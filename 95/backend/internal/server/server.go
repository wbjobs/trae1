package server

import (
	"context"
	"crypto/tls"
	"encoding/binary"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/quic-go/webtransport-go"

	"cloud-gamepad/internal/interpolator"
	"cloud-gamepad/internal/mapping"
	"cloud-gamepad/internal/protocol"
	"cloud-gamepad/internal/reorder"
	"cloud-gamepad/internal/rollback"
	"cloud-gamepad/internal/vgamepad"
)

type Config struct {
	Addr                 string
	CertFile             string
	KeyFile              string
	MaxGamepads          int
	ReorderBuffer        int
	ReorderTimeoutMs     int
	PredictionWindowMs   int
	CorrectionThreshold  float64
}

type Server struct {
	cfg        Config
	wt         *webtransport.Server
	manager    *vgamepad.Manager
	rollback   *rollback.Manager
	sessions   sync.Map
	done       chan struct{}
	playerMu   sync.Mutex
	nextPlayer int
	broadcast  chan []byte
}

type broadcaster struct {
	srv *Server
}

func (b *broadcaster) SendDatagram(data []byte) error {
	b.srv.sessions.Range(func(_, v any) bool {
		gs := v.(*gamepadSession)
		select {
		case gs.sendCh <- data:
		default:
		}
		return true
	})
	return nil
}

type gamepadSession struct {
	id          string
	sess        *webtransport.Session
	device      vgamepad.Device
	interp      *interpolator.Interpolator
	layout      mapping.Layout
	layoutMu    sync.RWMutex
	lastStats   Stats
	statsMu     sync.RWMutex
	cancel      context.CancelFunc
	sendCh      chan []byte
	reorderWin  *reorder.Window
	playerID    int
	mu          sync.Mutex
	seq         uint64
	lastInSeq   uint64
	lost        uint32
	total       uint32
	reorderEvents uint64
	pendingPing map[uint64]uint64
	pingMu      sync.Mutex
	srv         *Server
}

type Stats struct {
	ReceivedSeq    uint64  `json:"receivedSeq"`
	LastRTTms      uint16  `json:"lastRttMs"`
	LostPackets    uint32  `json:"lostPackets"`
	TotalPackets   uint32  `json:"totalPackets"`
	LossPct        float32 `json:"lossPct"`
	ReorderEvents  uint64  `json:"reorderEvents"`
}

func New(cfg Config) (*Server, error) {
	cert, err := tls.LoadX509KeyPair(cfg.CertFile, cfg.KeyFile)
	if err != nil {
		return nil, err
	}
	tlsCfg := &tls.Config{
		Certificates: []tls.Certificate{cert},
		NextProtos:   []string{"gamepad"},
	}
	wt := &webtransport.Server{
		H3: webtransport.H3Config{
			TLSConfig: tlsCfg,
		},
		CheckOrigin: func(r *http.Request) bool { return true },
	}
	mux := http.NewServeMux()
	s := &Server{
		cfg:       cfg,
		wt:        wt,
		manager:   vgamepad.NewManager(cfg.MaxGamepads, vgamepad.NewAutoFactory()),
		done:      make(chan struct{}),
		broadcast: make(chan []byte, 64),
	}
	s.rollback = rollback.NewManager(&broadcaster{srv: s})
	if cfg.CorrectionThreshold > 0 {
		s.rollback.SetCorrectionThreshold(cfg.CorrectionThreshold)
	}
	mux.HandleFunc("/wt", s.handleUpgrade)
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"status":"ok"}`))
	})
	wt.H3.Handler = mux
	s.rollback.Start()
	return s, nil
}

func (s *Server) handleUpgrade(w http.ResponseWriter, r *http.Request) {
	sess, err := s.wt.Upgrade(w, r)
	if err != nil {
		log.Printf("upgrade failed: %v", err)
		return
	}
	go s.serveSession(sess)
}

func (s *Server) serveSession(sess *webtransport.Session) {
	defer sess.CloseWithError(0, "")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	s.playerMu.Lock()
	playerID := s.nextPlayer
	s.nextPlayer++
	s.playerMu.Unlock()

	gs := &gamepadSession{
		id:          sess.RemoteAddr().String(),
		sess:        sess,
		interp:      interpolator.New(),
		layout:      mapping.LayoutXbox,
		cancel:      cancel,
		sendCh:      make(chan []byte, 256),
		pendingPing: make(map[uint64]uint64),
		playerID:    playerID,
		srv:         s,
	}
	s.sessions.Store(gs.id, gs)
	defer s.sessions.Delete(gs.id)

	device, err := s.manager.Acquire()
	if err != nil {
		log.Printf("acquire device failed: %v", err)
		return
	}
	gs.device = device
	defer s.manager.Release(device)

	s.rollback.AddPlayer(playerID)
	defer s.rollback.RemovePlayer(playerID)

	bufSize := s.cfg.ReorderBuffer
	if bufSize <= 0 {
		bufSize = reorder.DefaultWindowSize
	}
	timeoutMs := s.cfg.ReorderTimeoutMs
	if timeoutMs <= 0 {
		timeoutMs = reorder.DefaultTimeoutMs
	}
	gs.reorderWin = reorder.New(bufSize, timeoutMs, gs.applyOrderedPacket)
	gs.reorderWin.OnEvent(func(e reorder.Event) {
		gs.mu.Lock()
		gs.reorderEvents++
		gs.mu.Unlock()
	})
	gs.reorderWin.Start()
	defer gs.reorderWin.Stop()

	log.Printf("session %s started (pad %d, player=%d, reorder=%d, timeout=%dms)",
		gs.id, device.Index(), playerID, bufSize, timeoutMs)

	go gs.sendLoop(ctx)
	go gs.pingLoop(ctx)
	go gs.statsLoop(ctx)
	gs.readLoop(ctx)
}

func (gs *gamepadSession) readLoop(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		default:
		}
		data, err := gs.sess.ReceiveDatagram(ctx)
		if err != nil {
			if ctx.Err() != nil {
				return
			}
			if errors.Is(err, io.EOF) {
				return
			}
			continue
		}
		if len(data) == 0 {
			continue
		}
		switch data[0] {
		case protocol.MsgTypeInput:
			gs.handleInput(data)
		case protocol.MsgTypePing:
			gs.handlePing(data)
		case protocol.MsgTypePong:
			gs.handlePong(data)
		case protocol.MsgTypeMapping:
			gs.handleMapping(data)
		case protocol.MsgTypePlayerPos:
			gs.handlePlayerPos(data)
		}
	}
}

func (gs *gamepadSession) handleInput(data []byte) {
	p, err := protocol.DecodeInput(data)
	if err != nil {
		return
	}
	gs.mu.Lock()
	gs.total++
	gs.mu.Unlock()

	if gs.reorderWin != nil {
		gs.reorderWin.Push(p)
	} else {
		gs.applyPacket(p)
	}
}

func (gs *gamepadSession) applyOrderedPacket(p *protocol.InputPacket) {
	gs.applyPacket(p)
}

func (gs *gamepadSession) applyPacket(p *protocol.InputPacket) {
	gs.mu.Lock()
	seq16 := uint16(p.Seq & 0xFFFF)
	last16 := uint16(gs.lastInSeq & 0xFFFF)
	diff := int(int16(seq16 - last16))
	if diff > 1 && last16 != 0 {
		gs.lost += uint32(diff - 1)
	}
	if (diff > 0 || diff < -32000) || gs.lastInSeq == 0 {
		gs.lastInSeq = uint64(seq16)
	}
	gs.mu.Unlock()

	gs.interp.Push(p)

	btns := p.Buttons[:]
	axes := p.Axes[:]
	gs.layoutMu.RLock()
	l := gs.layout
	gs.layoutMu.RUnlock()
	rb, ra := mapping.Remap(btns, axes, l)

	state := vgamepad.State{
		Buttons:  rb,
		AxisLX:   ra[0],
		AxisLY:   ra[1],
		AxisRX:   ra[2],
		AxisRY:   ra[3],
		TriggerL: ra[4],
		TriggerR: ra[5],
	}
	if gs.device != nil {
		_ = gs.device.Update(state)
	}

	if gs.srv != nil && gs.srv.rollback != nil {
		moveX := float64(p.Axes[0])
		moveY := float64(p.Axes[1])
		gs.srv.rollback.PushInput(gs.playerID, seq16, moveX, moveY, p.Buttons)
	}

	ack := protocol.EncodeAck(p.Seq)
	select {
	case gs.sendCh <- ack:
	default:
	}
}

func (gs *gamepadSession) handlePlayerPos(data []byte) {
	p, err := protocol.DecodePlayerPos(data)
	if err != nil {
		return
	}
	if gs.srv != nil && gs.srv.rollback != nil {
		gs.srv.rollback.CheckCorrection(gs.playerID, p.Seq, p.PredictedX, p.PredictedY)
	}
}

func (gs *gamepadSession) handlePing(data []byte) {
	p, err := protocol.DecodePing(data)
	if err != nil {
		return
	}
	resp := protocol.EncodePong(p)
	select {
	case gs.sendCh <- resp:
	default:
	}
}

func (gs *gamepadSession) handlePong(data []byte) {
	seq, _, _, err := protocol.DecodePong(data)
	if err != nil {
		return
	}
	gs.pingMu.Lock()
	start, ok := gs.pendingPing[seq]
	if ok {
		delete(gs.pendingPing, seq)
	}
	gs.pingMu.Unlock()
	if !ok {
		return
	}
	rtt := uint16(uint64(time.Now().UnixMilli()) - start)
	gs.statsMu.Lock()
	gs.lastStats.LastRTTms = rtt
	gs.lastStats.ReceivedSeq = gs.readLastInSeq()
	gs.lastStats.LostPackets = gs.readLost()
	gs.lastStats.TotalPackets = gs.readTotal()
	gs.lastStats.ReorderEvents = gs.readReorderEvents()
	if gs.lastStats.TotalPackets > 0 {
		gs.lastStats.LossPct = float32(gs.lastStats.LostPackets) / float32(gs.lastStats.TotalPackets) * 100
	}
	gs.statsMu.Unlock()
}

func (gs *gamepadSession) handleMapping(data []byte) {
	var m struct {
		Layout string `json:"layout"`
	}
	if err := json.Unmarshal(data[1:], &m); err == nil && m.Layout != "" {
		gs.layoutMu.Lock()
		gs.layout = mapping.Layout(m.Layout)
		gs.layoutMu.Unlock()
	}
}

func (gs *gamepadSession) sendLoop(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case data := <-gs.sendCh:
			_ = gs.sess.SendDatagram(data)
		}
	}
}

func (gs *gamepadSession) pingLoop(ctx context.Context) {
	t := time.NewTicker(500 * time.Millisecond)
	defer t.Stop()
	var seq uint64
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			seq++
			ts := uint64(time.Now().UnixMilli())
			gs.pingMu.Lock()
			gs.pendingPing[seq] = ts
			gs.pingMu.Unlock()
			p := &protocol.PingPacket{Seq: seq, Timestamp: ts}
			data := protocol.EncodePing(p)
			select {
			case gs.sendCh <- data:
			default:
			}
		}
	}
}

func (gs *gamepadSession) statsLoop(ctx context.Context) {
	t := time.NewTicker(250 * time.Millisecond)
	defer t.Stop()
	var seq uint64
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			seq++
			sp := &protocol.StatsPacket{
				Seq:           seq,
				ReceivedSeq:   gs.readLastInSeq(),
				LastRTTms:     gs.readLastRTT(),
				LostPackets:   gs.readLost(),
				TotalPackets:  gs.readTotal(),
				ReorderEvents: gs.readReorderEvents(),
			}
			data := protocol.EncodeStats(sp)
			select {
			case gs.sendCh <- data:
			default:
			}
		}
	}
}

func (gs *gamepadSession) readLastRTT() uint16 {
	gs.statsMu.RLock()
	defer gs.statsMu.RUnlock()
	return gs.lastStats.LastRTTms
}

func (gs *gamepadSession) readLastInSeq() uint64 {
	gs.mu.Lock()
	defer gs.mu.Unlock()
	return gs.lastInSeq
}
func (gs *gamepadSession) readLost() uint32 {
	gs.mu.Lock()
	defer gs.mu.Unlock()
	return gs.lost
}
func (gs *gamepadSession) readTotal() uint32 {
	gs.mu.Lock()
	defer gs.mu.Unlock()
	return gs.total
}
func (gs *gamepadSession) readReorderEvents() uint64 {
	gs.mu.Lock()
	defer gs.mu.Unlock()
	return gs.reorderEvents
}

func (s *Server) Sessions() []*gamepadSession {
	var out []*gamepadSession
	s.sessions.Range(func(_, v any) bool {
		out = append(out, v.(*gamepadSession))
		return true
	})
	return out
}

func (s *Server) Run() error {
	log.Printf("cloud-gamepad server listening on %s (max pads=%d)", s.cfg.Addr, s.cfg.MaxGamepads)
	return s.wt.ListenAndServe(s.cfg.Addr, nil)
}

func (s *Server) Close() error {
	close(s.done)
	return nil
}
