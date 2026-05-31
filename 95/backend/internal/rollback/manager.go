package rollback

import (
	"context"
	"log"
	"sync"
	"time"

	"cloud-gamepad/internal/physics"
	"cloud-gamepad/internal/protocol"
)

type InputRecord struct {
	Frame uint64
	Input physics.Input
}

type SessionSender interface {
	SendDatagram([]byte) error
}

type Manager struct {
	mu             sync.Mutex
	world          *physics.World
	inputHistory   map[int][]InputRecord
	playerFrames   map[int]uint64
	latestFrame    map[int]protocol.EntityState
	sender         SessionSender
	correctionThld float64
	historySize    int
	running        bool
	ctx            context.Context
	cancel         context.CancelFunc
}

func NewManager(sender SessionSender) *Manager {
	ctx, cancel := context.WithCancel(context.Background())
	return &Manager{
		world:          physics.NewWorld(),
		inputHistory:   make(map[int][]InputRecord),
		playerFrames:   make(map[int]uint64),
		latestFrame:    make(map[int]protocol.EntityState),
		sender:         sender,
		correctionThld: 0.5,
		historySize:    120,
		ctx:            ctx,
		cancel:         cancel,
	}
}

func (m *Manager) SetCorrectionThreshold(v float64) { m.correctionThld = v }
func (m *Manager) SetHistorySize(v int)            { m.historySize = v }

func (m *Manager) Start() {
	m.mu.Lock()
	if m.running {
		m.mu.Unlock()
		return
	}
	m.running = true
	m.mu.Unlock()
	go m.loop()
}

func (m *Manager) Stop() {
	m.cancel()
	m.mu.Lock()
	m.running = false
	m.mu.Unlock()
}

func (m *Manager) AddPlayer(id int) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.world.AddPlayer(id)
	if _, ok := m.inputHistory[id]; !ok {
		m.inputHistory[id] = make([]InputRecord, 0, m.historySize)
	}
	m.playerFrames[id] = 0
}

func (m *Manager) RemovePlayer(id int) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.world.RemovePlayer(id)
	delete(m.inputHistory, id)
	delete(m.playerFrames, id)
}

func (m *Manager) PushInput(playerID int, seq uint16, moveX, moveY float64, buttons [17]bool) {
	m.mu.Lock()
	defer m.mu.Unlock()

	frame := m.world.FrameNumber()
	input := physics.Input{
		MoveX:    moveX,
		MoveY:    moveY,
		Buttons:  buttons,
		Sequence: seq,
	}

	hist := m.inputHistory[playerID]
	hist = append(hist, InputRecord{Frame: frame, Input: input})
	if len(hist) > m.historySize {
		hist = hist[len(hist)-m.historySize:]
	}
	m.inputHistory[playerID] = hist

	if m.playerFrames[playerID] == frame {
		m.playerFrames[playerID] = frame + 1
	}
}

func (m *Manager) loop() {
	ticker := time.NewTicker(time.Duration(physics.FixedTimestep * 1e9))
	defer ticker.Stop()
	for {
		select {
		case <-m.ctx.Done():
			return
		case <-ticker.C:
			m.step()
		}
	}
}

func (m *Manager) step() {
	m.mu.Lock()
	inputs := make(map[int]physics.Input)
	for pid := range m.inputHistory {
		frame := m.world.FrameNumber()
		var latest *InputRecord
		for i := len(m.inputHistory[pid]) - 1; i >= 0; i-- {
			ir := &m.inputHistory[pid][i]
			if ir.Frame <= frame {
				latest = ir
				break
			}
		}
		if latest != nil {
			inputs[pid] = latest.Input
		}
	}
	m.world.Step(inputs)

	snapshot := m.world.Snapshot()
	for _, e := range snapshot {
		m.latestFrame[e.PlayerID] = protocol.EntityState{
			PlayerID: int32(e.PlayerID),
			PosX:     e.Pos.X,
			PosY:     e.Pos.Y,
			VelX:     e.Vel.X,
			VelY:     e.Vel.Y,
		}
	}
	currentFrame := m.world.FrameNumber()
	m.mu.Unlock()

	statePkt := &protocol.StatePacket{
		Frame:     currentFrame,
		Timestamp: uint64(time.Now().UnixMilli()),
	}
	m.mu.Lock()
	for _, s := range m.latestFrame {
		statePkt.Entities = append(statePkt.Entities, s)
	}
	m.mu.Unlock()

	if m.sender != nil {
		data := protocol.EncodeState(statePkt)
		_ = m.sender.SendDatagram(data)
	}
}

func (m *Manager) CheckCorrection(playerID int, clientSeq uint16, clientX, clientY float64) {
	m.mu.Lock()
	defer m.mu.Unlock()
	s, ok := m.latestFrame[playerID]
	if !ok {
		return
	}
	dx := s.PosX - clientX
	dy := s.PosY - clientY
	distSq := dx*dx + dy*dy
	if distSq > m.correctionThld*m.correctionThld {
		corr := &protocol.CorrectionPacket{
			PlayerID:    int32(playerID),
			Frame:       m.world.FrameNumber(),
			CorrectPosX: s.PosX,
			CorrectPosY: s.PosY,
			CorrectVelX: s.VelX,
			CorrectVelY: s.VelY,
			YourPosX:    clientX,
			YourPosY:    clientY,
		}
		if m.sender != nil {
			data := protocol.EncodeCorrection(corr)
			_ = m.sender.SendDatagram(data)
			log.Printf("[rollback] correction sent: player=%d diff=%.2f", playerID, distSq)
		}
	}
}

func (m *Manager) WorldSnapshot() []physics.Entity {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.world.Snapshot()
}

func (m *Manager) Frame() uint64 {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.world.FrameNumber()
}
