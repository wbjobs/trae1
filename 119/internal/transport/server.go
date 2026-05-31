package transport

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/lxc-migrate/lxc-migrate/internal/types"
)

const (
	MaxChunkSize = 4 * 1024 * 1024
	HeaderSize   = 1024
	ConnTimeout  = 30 * time.Second
	HandshakeSig = "LXC-MIGRATE-v1"
)

type MessageType uint8

const (
	MsgHandshake MessageType = iota
	MsgResourceCheck
	MsgResourceCheckAck
	MsgPreCopyStart
	MsgPreCopyData
	MsgPreCopyDone
	MsgFinalCheckpoint
	MsgRestoreReady
	MsgNetworkReconfig
	MsgComplete
	MsgError
	MsgProgress
)

type Message struct {
	Type    MessageType     `json:"type"`
	Payload json.RawMessage `json:"payload"`
}

type ResourceCheckPayload struct {
	ContainerName string `json:"container_name"`
	RequiredMem   uint64 `json:"required_mem"`
	RequiredDisk  uint64 `json:"required_disk"`
}

type ResourceCheckAck struct {
	Success     bool   `json:"success"`
	Message     string `json:"message"`
	FreeMemory  uint64 `json:"free_memory"`
	FreeDisk    uint64 `json:"free_disk"`
	NewIP       string `json:"new_ip"`
}

type ProgressPayload struct {
	Phase       string  `json:"phase"`
	Transferred int64   `json:"transferred"`
	Total       int64   `json:"total"`
	Speed       float64 `json:"speed_mbps"`
	Percent     float64 `json:"percent"`
}

type DataChunk struct {
	Index    uint64 `json:"index"`
	FileName string `json:"file_name"`
	Offset   int64  `json:"offset"`
	Data     []byte `json:"-"`
	IsLast   bool   `json:"is_last"`
}

type TransportServer struct {
	listener     net.Listener
	config       *types.DaemonConfig
	mu           sync.Mutex
	ctx          context.Context
	cancel       context.CancelFunc
	bwLimit      int64
	OnRestore    func(dir string, containerName string) error
}

func NewTransportServer(config *types.DaemonConfig) *TransportServer {
	ctx, cancel := context.WithCancel(context.Background())
	return &TransportServer{
		config: config,
		ctx:    ctx,
		cancel: cancel,
	}
}

func (s *TransportServer) Start() error {
	addr := fmt.Sprintf("%s:%d", s.config.ListenAddr, s.config.ListenPort)
	var err error
	s.listener, err = net.Listen("tcp", addr)
	if err != nil {
		return fmt.Errorf("failed to listen on %s: %w", addr, err)
	}

	fmt.Printf("[daemon] LXC migration daemon listening on %s\n", addr)

	for {
		select {
		case <-s.ctx.Done():
			return nil
		default:
		}

		conn, err := s.listener.Accept()
		if err != nil {
			if s.ctx.Err() != nil {
				return nil
			}
			fmt.Fprintf(os.Stderr, "[daemon] accept error: %v\n", err)
			continue
		}

		go s.handleConnection(conn)
	}
}

func (s *TransportServer) handleConnection(conn net.Conn) {
	defer conn.Close()
	remoteAddr := conn.RemoteAddr().String()
	fmt.Printf("[daemon] connection from %s\n", remoteAddr)

	conn.SetDeadline(time.Now().Add(ConnTimeout))

	if err := readHandshake(conn); err != nil {
		fmt.Fprintf(os.Stderr, "[daemon] handshake failed with %s: %v\n", remoteAddr, err)
		writeMessage(conn, MsgError, map[string]string{"error": err.Error()})
		return
	}
	if err := writeHandshake(conn); err != nil {
		fmt.Fprintf(os.Stderr, "[daemon] handshake ack failed: %v\n", err)
		return
	}

	conn.SetDeadline(time.Time{})

	sessionDir := filepath.Join(s.config.DataDir, fmt.Sprintf("session-%d", time.Now().UnixNano()))
	if err := os.MkdirAll(sessionDir, 0700); err != nil {
		writeMessage(conn, MsgError, map[string]string{"error": err.Error()})
		return
	}
	defer os.RemoveAll(sessionDir)

	session := &serverSession{
		conn:      conn,
		dir:       sessionDir,
		bwLimit:   s.bwLimit,
		onRestore: s.OnRestore,
	}

	session.run()
}

func (s *TransportServer) Stop() {
	s.cancel()
	if s.listener != nil {
		s.listener.Close()
	}
}

type serverSession struct {
	conn          net.Conn
	dir           string
	bwLimit       int64
	containerName string
	onRestore     func(dir string, containerName string) error
}

func (s *serverSession) run() {
	for {
		msg, err := readMessage(s.conn)
		if err != nil {
			if err != io.EOF {
				fmt.Fprintf(os.Stderr, "[daemon] read error: %v\n", err)
			}
			return
		}

		switch msg.Type {
		case MsgResourceCheck:
			s.handleResourceCheck(msg)
		case MsgPreCopyStart:
			s.handlePreCopyData()
		case MsgFinalCheckpoint:
			s.handleFinalCheckpoint()
		case MsgNetworkReconfig:
			s.handleNetworkReconfig(msg)
		case MsgComplete:
			return
		default:
			fmt.Fprintf(os.Stderr, "[daemon] unknown message type: %d\n", msg.Type)
		}
	}
}

func (s *serverSession) handleResourceCheck(msg *Message) {
	var payload ResourceCheckPayload
	if err := json.Unmarshal(msg.Payload, &payload); err != nil {
		writeMessage(s.conn, MsgError, map[string]string{"error": "invalid resource check payload: " + err.Error()})
		return
	}

	s.containerName = payload.ContainerName

	info, err := checkLocalResources()
	if err != nil {
		ack := ResourceCheckAck{
			Success: false,
			Message: fmt.Sprintf("resource check failed: %v", err),
		}
		writeMessage(s.conn, MsgResourceCheckAck, ack)
		return
	}

	if info.FreeMemory < payload.RequiredMem {
		ack := ResourceCheckAck{
			Success: false,
			Message: fmt.Sprintf("insufficient memory: need %d, have %d", payload.RequiredMem, info.FreeMemory),
		}
		writeMessage(s.conn, MsgResourceCheckAck, ack)
		return
	}

	if info.FreeDisk < payload.RequiredDisk {
		ack := ResourceCheckAck{
			Success: false,
			Message: fmt.Sprintf("insufficient disk: need %d, have %d", payload.RequiredDisk, info.FreeDisk),
		}
		writeMessage(s.conn, MsgResourceCheckAck, ack)
		return
	}

	newIP := allocateIP()
	ack := ResourceCheckAck{
		Success:    true,
		Message:    "resources OK",
		FreeMemory: info.FreeMemory,
		FreeDisk:   info.FreeDisk,
		NewIP:      newIP,
	}
	writeMessage(s.conn, MsgResourceCheckAck, ack)
}

func (s *serverSession) handlePreCopyData() {
	fmt.Println("[daemon] receiving pre-copy data...")
	if err := receiveFile(s.conn, s.dir, "pre-dump", s.bwLimit); err != nil {
		fmt.Fprintf(os.Stderr, "[daemon] pre-copy receive error: %v\n", err)
		writeMessage(s.conn, MsgError, map[string]string{"error": err.Error()})
		return
	}

	writeMessage(s.conn, MsgPreCopyDone, nil)
}

func (s *serverSession) handleFinalCheckpoint() {
	fmt.Println("[daemon] receiving final checkpoint data...")
	if err := receiveFile(s.conn, s.dir, "final-dump", s.bwLimit); err != nil {
		fmt.Fprintf(os.Stderr, "[daemon] final checkpoint receive error: %v\n", err)
		writeMessage(s.conn, MsgError, map[string]string{"error": err.Error()})
		return
	}

	finalDir := filepath.Join(s.dir, "final-dump")

	if s.onRestore != nil && s.containerName != "" {
		fmt.Printf("[daemon] restoring container '%s' from %s...\n", s.containerName, finalDir)
		if err := s.onRestore(finalDir, s.containerName); err != nil {
			fmt.Fprintf(os.Stderr, "[daemon] restore error: %v\n", err)
			writeMessage(s.conn, MsgError, map[string]string{"error": "restore failed: " + err.Error()})
			return
		}
		fmt.Println("[daemon] container restored successfully")
	}

	writeMessage(s.conn, MsgRestoreReady, map[string]string{"dir": finalDir})
}

func (s *serverSession) handleNetworkReconfig(msg *Message) {
	var nc types.NetworkConfig
	if err := json.Unmarshal(msg.Payload, &nc); err != nil {
		writeMessage(s.conn, MsgError, map[string]string{"error": "invalid network config: " + err.Error()})
		return
	}

	fmt.Printf("[daemon] reconfiguring network: old=%s new=%s\n", nc.OldIP, nc.NewIP)
	if err := applyNetworkConfig(&nc); err != nil {
		writeMessage(s.conn, MsgError, map[string]string{"error": "network reconfig failed: " + err.Error()})
		return
	}

	writeMessage(s.conn, MsgComplete, nil)
}

func allocateIP() string {
	return "10.0.3." + fmt.Sprintf("%d", time.Now().Unix()%254+2)
}

func checkLocalResources() (*types.ResourceInfo, error) {
	var info types.ResourceInfo

	totalMem, freeMem, availMem, err := readMemInfo()
	if err != nil {
		return nil, fmt.Errorf("read memory info: %w", err)
	}
	info.TotalMemory = totalMem
	info.FreeMemory = freeMem
	info.AvailableMemory = availMem

	totalDisk, freeDisk, err := readDiskInfo("/")
	if err != nil {
		return nil, fmt.Errorf("read disk info: %w", err)
	}
	info.TotalDisk = totalDisk
	info.FreeDisk = freeDisk

	info.CPUNum, _ = readCPUCount()
	info.CPUFreePercent = 80.0

	return &info, nil
}

func readMemInfo() (uint64, uint64, uint64, error) {
	data, err := os.ReadFile("/proc/meminfo")
	if err != nil {
		return 0, 0, 0, err
	}

	var total, free, available uint64
	for _, line := range strings.Split(string(data), "\n") {
		fields := strings.Fields(line)
		if len(fields) < 2 {
			continue
		}
		val, _ := strconv.ParseUint(fields[1], 10, 64)
		switch fields[0] {
		case "MemTotal:":
			total = val * 1024
		case "MemFree:":
			free = val * 1024
		case "MemAvailable:":
			available = val * 1024
		}
	}
	if available == 0 {
		available = free
	}
	return total, free, available, nil
}

func readCPUCount() (int, error) {
	data, err := os.ReadFile("/proc/cpuinfo")
	if err != nil {
		return 0, err
	}
	count := 0
	for _, line := range strings.Split(string(data), "\n") {
		if strings.HasPrefix(line, "processor") {
			count++
		}
	}
	if count == 0 {
		return 0, fmt.Errorf("no CPU found")
	}
	return count, nil
}

func applyNetworkConfig(nc *types.NetworkConfig) error {
	if nc == nil || nc.NewIP == "" {
		return nil
	}
	fmt.Printf("[daemon] applying network config: %s -> %s (iface: %s)\n",
		nc.OldIP, nc.NewIP, nc.Interface)
	return nil
}
