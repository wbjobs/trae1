package bgp

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/netip"
	"os"
	"strconv"
	"sync"
	"time"

	api "github.com/osrg/gobgp/v3/api"
	"github.com/osrg/gobgp/v3/pkg/server"
	"google.golang.org/protobuf/types/known/anypb"

	"iprep-sync/internal/config"
	"iprep-sync/internal/model"
	"iprep-sync/internal/store"
)

type SessionState int

const (
	SessionUnknown SessionState = iota
	SessionUp
	SessionDown
)

func (s SessionState) String() string {
	switch s {
	case SessionUp:
		return "UP"
	case SessionDown:
		return "DOWN"
	default:
		return "UNKNOWN"
	}
}

type PeerSession struct {
	Name          string
	Address       string
	State         SessionState
	LastState     SessionState
	DownSince     time.Time
	LastUp        time.Time
	LastRefresh   time.Time
	InterruptionCount int64
}

type InterruptionLog struct {
	Peer      string    `json:"peer"`
	Address   string    `json:"address"`
	DownAt    time.Time `json:"down_at"`
	UpAt      time.Time `json:"up_at"`
	Duration  float64   `json:"duration_seconds"`
	Recovered bool      `json:"recovered"`
}

type Manager struct {
	cfg    *config.Config
	store  *store.Store
	bgpSrv *server.BgpServer

	mu         sync.Mutex
	peerCounts map[string]int64

	sessions   map[string]*PeerSession
	interrupts []InterruptionLog
	intLogger  *log.Logger
}

func New(cfg *config.Config, st *store.Store) *Manager {
	intLogger := log.New(os.Stdout, "[bgp-int] ", log.LstdFlags|log.Lmicroseconds)
	return &Manager{
		cfg:        cfg,
		store:      st,
		peerCounts: map[string]int64{},
		sessions:   map[string]*PeerSession{},
		interrupts: []InterruptionLog{},
		intLogger:  intLogger,
	}
}

type threatAnnotation struct {
	Community string `json:"community,omitempty"`
	Flowspec  string `json:"flowspec,omitempty"`
}

func (m *Manager) Start(ctx context.Context) error {
	m.bgpSrv = server.NewBgpServer()
	go m.bgpSrv.Serve()

	if _, err := m.bgpSrv.StartBgp(ctx, &api.StartBgpRequest{
		Global: &api.Global{
			Asn:        m.cfg.Local.ASN,
			RouterId:   m.cfg.Local.RouterID,
			ListenPort: int32(m.cfg.Local.ListenPort),
			ListenAddresses: []string{m.cfg.Local.ListenHost},
		},
	}); err != nil {
		return fmt.Errorf("start bgp: %w", err)
	}

	for _, p := range m.cfg.Peers {
		m.sessions[p.Name] = &PeerSession{
			Name:      p.Name,
			Address:   p.Address,
			State:     SessionUnknown,
			LastState: SessionUnknown,
		}

		peer := &api.Peer{
			State: &api.PeerState{PeerType: api.PeerState_EXTERNAL},
			Conf: &api.PeerConf{
				NeighborAddress: p.Address,
				PeerAsn:         p.ASN,
				NeighborPort:    uint32(p.Port),
			},
			AfiSafis: []*api.AfiSafi{
				{Config: &api.AfiSafiConfig{Family: &api.Family{Afi: api.Family_AFI_IP, Safi: api.Family_SAFI_UNICAST}, Enabled: true}},
				{Config: &api.AfiSafiConfig{Family: &api.Family{Afi: api.Family_AFI_IP6, Safi: api.Family_SAFI_UNICAST}, Enabled: true}},
				{Config: &api.AfiSafiConfig{Family: &api.Family{Afi: api.Family_AFI_IP, Safi: api.Family_SAFI_FLOW_SPEC_UNICAST}, Enabled: true}},
				{Config: &api.AfiSafiConfig{Family: &api.Family{Afi: api.Family_AFI_IP6, Safi: api.Family_SAFI_FLOW_SPEC_UNICAST}, Enabled: true}},
			},
		}
		if _, err := m.bgpSrv.AddPeer(ctx, &api.AddPeerRequest{Peer: peer}); err != nil {
			return fmt.Errorf("add peer %s: %w", p.Name, err)
		}
	}

	// monitor events (peer up/down)
	monReq := &api.MonitorPeerStateRequest{
		InitialState: true,
	}
	if err := m.bgpSrv.MonitorPeerState(ctx, monReq, func(ev *api.WatchEvent) {
		if ps := ev.GetPeer(); ps != nil {
			m.handlePeerStateChange(ctx, ps)
		}
	}); err != nil {
		return fmt.Errorf("monitor peer: %w", err)
	}

	// monitor table updates for unicast + flowspec
	tblReq := &api.MonitorTableRequest{
		TableType: api.TableType_ADJ_IN,
		Family:    &api.Family{Afi: api.Family_AFI_IP, Safi: api.Family_SAFI_UNICAST},
	}
	if err := m.bgpSrv.MonitorTable(ctx, tblReq, func(ev *api.WatchEvent) {
		m.handleTableEvent(ctx, ev)
	}); err != nil {
		return fmt.Errorf("monitor table v4 unicast: %w", err)
	}
	tblReq6 := &api.MonitorTableRequest{
		TableType: api.TableType_ADJ_IN,
		Family:    &api.Family{Afi: api.Family_AFI_IP6, Safi: api.Family_SAFI_UNICAST},
	}
	if err := m.bgpSrv.MonitorTable(ctx, tblReq6, func(ev *api.WatchEvent) {
		m.handleTableEvent(ctx, ev)
	}); err != nil {
		return fmt.Errorf("monitor table v6 unicast: %w", err)
	}
	tblReqF := &api.MonitorTableRequest{
		TableType: api.TableType_ADJ_IN,
		Family:    &api.Family{Afi: api.Family_AFI_IP, Safi: api.Family_SAFI_FLOW_SPEC_UNICAST},
	}
	if err := m.bgpSrv.MonitorTable(ctx, tblReqF, func(ev *api.WatchEvent) {
		m.handleTableEvent(ctx, ev)
	}); err != nil {
		return fmt.Errorf("monitor table v4 flowspec: %w", err)
	}
	tblReqF6 := &api.MonitorTableRequest{
		TableType: api.TableType_ADJ_IN,
		Family:    &api.Family{Afi: api.Family_AFI_IP6, Safi: api.Family_SAFI_FLOW_SPEC_UNICAST},
	}
	if err := m.bgpSrv.MonitorTable(ctx, tblReqF6, func(ev *api.WatchEvent) {
		m.handleTableEvent(ctx, ev)
	}); err != nil {
		return fmt.Errorf("monitor table v6 flowspec: %w", err)
	}

	// wait for initial sync window
	wait := time.Duration(m.cfg.Sync.StartupWait) * time.Second
	log.Printf("[bgp] waiting %s for initial full-table sync", wait)
	time.Sleep(wait)
	log.Printf("[bgp] startup sync window closed")
	return nil
}

func (m *Manager) handleTableEvent(ctx context.Context, ev *api.WatchEvent) {
	for _, p := range ev.GetPathList() {
		m.handlePath(ctx, p)
	}
}

func (m *Manager) handlePath(ctx context.Context, p *api.Path) {
	peer := p.GetNeighborIp()
	peerASN := p.GetSourceAsn()
	peerName := m.peerNameByAddr(peer)

	// Determine family and extract target prefixes
	family := p.GetFamily()
	prefixes := m.extractPrefixes(p, family)
	if len(prefixes) == 0 {
		return
	}

	level, ann := m.extractThreatLevel(p)
	raw, _ := json.Marshal(ann)

	for _, pfx := range prefixes {
		if p.GetIsWithdraw() {
			if err := m.store.RemovePeerPrefix(ctx, peerName, pfx); err != nil {
				log.Printf("[bgp] remove peer=%s prefix=%s err=%v", peerName, pfx, err)
			} else {
				m.addPeerCount(peerName, -1)
				log.Printf("[bgp] withdraw peer=%s prefix=%s", peerName, pfx)
			}
			continue
		}
		if !model.ValidLevel(int(level)) {
			log.Printf("[bgp] skip peer=%s prefix=%s no threat community", peerName, pfx)
			continue
		}
		if err := m.store.UpsertPeerPrefix(ctx, peerName, peerASN, pfx, level, string(raw)); err != nil {
			log.Printf("[bgp] upsert peer=%s prefix=%s err=%v", peerName, pfx, err)
		} else {
			m.addPeerCount(peerName, 1)
			log.Printf("[bgp] announce peer=%s prefix=%s level=%d", peerName, pfx, level)
		}
	}
}

func (m *Manager) peerNameByAddr(addr string) string {
	for _, p := range m.cfg.Peers {
		if p.Address == addr {
			return p.Name
		}
	}
	return addr
}

func (m *Manager) addPeerCount(peer string, delta int64) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.peerCounts[peer] += delta
	if m.peerCounts[peer] < 0 {
		m.peerCounts[peer] = 0
	}
}

func (m *Manager) PeerCounts() map[string]int64 {
	m.mu.Lock()
	defer m.mu.Unlock()
	out := make(map[string]int64, len(m.peerCounts))
	for k, v := range m.peerCounts {
		out[k] = v
	}
	return out
}

// Sessions returns a snapshot of current peer session states
func (m *Manager) Sessions() map[string]PeerSession {
	m.mu.Lock()
	defer m.mu.Unlock()
	out := make(map[string]PeerSession, len(m.sessions))
	for k, v := range m.sessions {
		out[k] = *v
	}
	return out
}

// InterruptionLog returns recent interruption logs
func (m *Manager) InterruptionLogs() []InterruptionLog {
	m.mu.Lock()
	defer m.mu.Unlock()
	logs := make([]InterruptionLog, len(m.interrupts))
	copy(logs, m.interrupts)
	return logs
}

// handlePeerStateChange detects DOWN→UP transitions and triggers ROUTE-REFRESH
func (m *Manager) handlePeerStateChange(ctx context.Context, ps *api.PeerState) {
	m.mu.Lock()
	sess, ok := m.sessions[ps.Name]
	if !ok {
		sess = &PeerSession{
			Name:    ps.Name,
			Address: ps.NeighborAddress,
			State:   SessionUnknown,
		}
		m.sessions[ps.Name] = sess
	}
	sess.LastState = sess.State

	switch ps.SessionState {
	case api.PeerState_ESTABLISHED:
		sess.State = SessionUp
	default:
		sess.State = SessionDown
	}

	now := time.Now().UTC()

	if sess.State == SessionDown && sess.LastState != SessionDown {
		sess.DownSince = now
		sess.InterruptionCount++
		m.intLogger.Printf("peer %s (%s) went DOWN, interruption #%d",
			ps.Name, ps.NeighborAddress, sess.InterruptionCount)
		m.interrupts = append(m.interrupts, InterruptionLog{
			Peer:      ps.Name,
			Address:   ps.NeighborAddress,
			DownAt:    now,
			Recovered: false,
		})
	}

	if sess.State == SessionUp && sess.LastState == SessionDown {
		duration := now.Sub(sess.DownSince).Seconds()
		sess.LastUp = now
		sess.LastRefresh = now

		m.intLogger.Printf("peer %s (%s) came UP after %.1fs downtime, sending ROUTE-REFRESH",
			ps.Name, ps.NeighborAddress, duration)

		if len(m.interrupts) > 0 {
			last := &m.interrupts[len(m.interrupts)-1]
			if !last.Recovered && last.Peer == ps.Name {
				last.UpAt = now
				last.Duration = duration
				last.Recovered = true
			}
		}

		go m.sendRouteRefresh(ctx, ps.Name, ps.NeighborAddress)
	}

	m.mu.Unlock()

	log.Printf("[bgp] peer %s state=%s (prev=%s)", ps.Name, sess.State, sess.LastState)
}

// sendRouteRefresh sends ROUTE-REFRESH for all supported AFI/SAFI to request full table re-send
func (m *Manager) sendRouteRefresh(ctx context.Context, peerName, peerAddr string) {
	afisafis := []*api.Family{
		{Afi: api.Family_AFI_IP, Safi: api.Family_SAFI_UNICAST},
		{Afi: api.Family_AFI_IP6, Safi: api.Family_SAFI_UNICAST},
		{Afi: api.Family_AFI_IP, Safi: api.Family_SAFI_FLOW_SPEC_UNICAST},
		{Afi: api.Family_AFI_IP6, Safi: api.Family_SAFI_FLOW_SPEC_UNICAST},
	}

	successCount := 0
	for _, f := range afisafis {
		req := &api.RouteRefreshRequest{
			Address: peerAddr,
			Family:  f,
		}
		if err := m.bgpSrv.RouteRefresh(ctx, req); err != nil {
			m.intLogger.Printf("ROUTE-REFRESH to peer %s (%s) family=%v failed: %v",
				peerName, peerAddr, f, err)
		} else {
			successCount++
			m.intLogger.Printf("ROUTE-REFRESH to peer %s (%s) family=%v sent successfully",
				peerName, peerAddr, f)
		}
	}

	if successCount == len(afisafis) {
		m.intLogger.Printf("peer %s (%s) full ROUTE-REFRESH completed (%d/%d families)",
			peerName, peerAddr, successCount, len(afisafis))
	} else {
		m.intLogger.Printf("peer %s (%s) ROUTE-REFRESH partial: %d/%d families succeeded",
			peerName, peerAddr, successCount, len(afisafis))
	}
}

// extractPrefixes 从 path 中抽取受影响的 IP 前缀。
// 对 unicast 直接取 nlri；对 flowspec 从 match 字段中抽取源/目的前缀。
func (m *Manager) extractPrefixes(p *api.Path, f *api.Family) []netip.Prefix {
	out := make([]netip.Prefix, 0, 1)
	if f == nil {
		return out
	}
	switch f.Safi {
	case api.Family_SAFI_UNICAST:
		nlri := p.GetNlri()
		if ipPrefix, ok := nlri.(*api.IPAddressPrefix); ok {
			s := ipPrefix.GetPrefixLenStr()
			if s == "" {
				s = fmt.Sprintf("%s/%d", ipPrefix.GetPrefix(), ipPrefix.GetPrefixLen())
			}
			if pfx, err := netip.ParsePrefix(s); err == nil {
				out = append(out, pfx)
			}
		}
	case api.Family_SAFI_FLOW_SPEC_UNICAST, api.Family_SAFI_FLOW_SPEC_VPN:
		nlri := p.GetNlri()
		if fs, ok := nlri.(*api.FlowSpecNLRI); ok {
			for _, r := range fs.Rules {
				if t := r.GetType(); t == api.FlowSpecNLRI_Type_DestinationPrefix ||
					t == api.FlowSpecNLRI_Type_SourcePrefix {
					if pfx, err := netip.ParsePrefix(r.GetValue()); err == nil {
						out = append(out, pfx)
					}
				}
			}
		}
	}
	return out
}

// extractThreatLevel 解析 BGP Community / Flowspec 中的威胁等级
// 规则：Community 值为 <BaseASN>:<1..5>；Flowspec 中 traffic-rate 非 0 时取其值。
// 多个命中取最大值。
func (m *Manager) extractThreatLevel(p *api.Path) (model.ThreatLevel, threatAnnotation) {
	level := model.LevelUnknown
	var ann threatAnnotation
	base := m.cfg.ThreatCommunity.BaseASN

	for _, pattr := range p.GetPattrs() {
		switch v := pattr.(type) {
		case *api.CommunitiesAttribute:
			for _, c := range v.Communities {
				asn := (c >> 16) & 0xFFFF
				val := c & 0xFFFF
				if asn == base && model.ValidLevel(int(val)) {
					l := model.ThreatLevel(val)
					if l > level {
						level = l
					}
					ann.Community = fmt.Sprintf("%d:%d", asn, val)
				}
			}
		case *api.ExtendedCommunitiesAttribute:
			// 扩展 community 暂不处理；可扩展 traffic-rate 等
		}
	}

	// 解析 flowspec 的 traffic-rate 扩展 community
	for _, pattr := range p.GetPattrs() {
		if ext, ok := pattr.(*anypb.Any); ok {
			// any 类型的路径属性使用通用 proto 解包；这里忽略，交由 CommunitiesAttribute 处理
			_ = ext
		}
	}

	return level, ann
}

// parseNumericCommunity 解析形如 "as:val" 的 community 字符串为 (asn, val)
func parseNumericCommunity(s string) (uint32, uint32, error) {
	parts := splitColon(s)
	if len(parts) != 2 {
		return 0, 0, fmt.Errorf("bad community %q", s)
	}
	a, err := strconv.ParseUint(parts[0], 10, 32)
	if err != nil {
		return 0, 0, err
	}
	b, err := strconv.ParseUint(parts[1], 10, 32)
	if err != nil {
		return 0, 0, err
	}
	return uint32(a), uint32(b), nil
}

func splitColon(s string) []string {
	var out []string
	start := 0
	for i := 0; i < len(s); i++ {
		if s[i] == ':' {
			out = append(out, s[start:i])
			start = i + 1
		}
	}
	out = append(out, s[start:])
	return out
}
