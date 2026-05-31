package transport

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"time"

	"github.com/lxc-migrate/lxc-migrate/internal/types"
)

type ProgressCallback func(state *types.ProgressState)

type TransportClient struct {
	conn     net.Conn
	bwLimit  int64
	progress ProgressCallback
	host     string
	port     int
}

func NewTransportClient(host string, port int, bwLimit int64, progress ProgressCallback) *TransportClient {
	return &TransportClient{
		host:     host,
		port:     port,
		bwLimit:  bwLimit,
		progress: progress,
	}
}

func (c *TransportClient) Connect(ctx context.Context) error {
	addr := fmt.Sprintf("%s:%d", c.host, c.port)
	dialer := net.Dialer{Timeout: ConnTimeout}

	var err error
	c.conn, err = dialer.DialContext(ctx, "tcp", addr)
	if err != nil {
		return fmt.Errorf("connect to %s: %w", addr, err)
	}

	if err := writeHandshake(c.conn); err != nil {
		return fmt.Errorf("send handshake: %w", err)
	}

	if err := readHandshake(c.conn); err != nil {
		return fmt.Errorf("read handshake ack: %w", err)
	}

	return nil
}

func (c *TransportClient) Close() error {
	if c.conn != nil {
		return c.conn.Close()
	}
	return nil
}

func (c *TransportClient) CheckResources(containerName string, requiredMem uint64, requiredDisk uint64) (*ResourceCheckAck, error) {
	payload := ResourceCheckPayload{
		ContainerName: containerName,
		RequiredMem:   requiredMem,
		RequiredDisk:  requiredDisk,
	}

	if err := writeMessage(c.conn, MsgResourceCheck, payload); err != nil {
		return nil, fmt.Errorf("send resource check: %w", err)
	}

	resp, err := readMessage(c.conn)
	if err != nil {
		return nil, fmt.Errorf("read resource check response: %w", err)
	}

	if resp.Type == MsgError {
		var errResp struct{ Error string `json:"error"` }
		json.Unmarshal(resp.Payload, &errResp)
		return nil, fmt.Errorf("resource check error: %s", errResp.Error)
	}

	if resp.Type != MsgResourceCheckAck {
		return nil, fmt.Errorf("unexpected response type: %d", resp.Type)
	}

	var ack ResourceCheckAck
	if err := json.Unmarshal(resp.Payload, &ack); err != nil {
		return nil, fmt.Errorf("decode resource check ack: %w", err)
	}

	return &ack, nil
}

func (c *TransportClient) SendPreCopyData(sourceDir string, iter int) error {
	if err := writeMessage(c.conn, MsgPreCopyStart, map[string]int{"iter": iter}); err != nil {
		return fmt.Errorf("send pre-copy start: %w", err)
	}

	if err := sendDirectory(c.conn, sourceDir, c.bwLimit, c.progress, "pre-copy"); err != nil {
		return fmt.Errorf("send pre-copy data: %w", err)
	}

	resp, err := readMessage(c.conn)
	if err != nil {
		return fmt.Errorf("read pre-copy ack: %w", err)
	}

	if resp.Type == MsgError {
		var errResp struct{ Error string `json:"error"` }
		json.Unmarshal(resp.Payload, &errResp)
		return fmt.Errorf("pre-copy error: %s", errResp.Error)
	}

	if resp.Type != MsgPreCopyDone {
		return fmt.Errorf("unexpected response after pre-copy: %d", resp.Type)
	}

	return nil
}

func (c *TransportClient) SendFinalCheckpoint(sourceDir string) (string, error) {
	if err := writeMessage(c.conn, MsgFinalCheckpoint, map[string]string{}); err != nil {
		return "", fmt.Errorf("send final checkpoint: %w", err)
	}

	if err := sendDirectory(c.conn, sourceDir, c.bwLimit, c.progress, "final-checkpoint"); err != nil {
		return "", fmt.Errorf("send final checkpoint data: %w", err)
	}

	resp, err := readMessage(c.conn)
	if err != nil {
		return "", fmt.Errorf("read restore ready: %w", err)
	}

	if resp.Type == MsgError {
		var errResp struct{ Error string `json:"error"` }
		json.Unmarshal(resp.Payload, &errResp)
		return "", fmt.Errorf("restore error: %s", errResp.Error)
	}

	if resp.Type != MsgRestoreReady {
		return "", fmt.Errorf("unexpected response: %d", resp.Type)
	}

	var dirResp struct{ Dir string `json:"dir"` }
	if err := json.Unmarshal(resp.Payload, &dirResp); err != nil {
		return "", fmt.Errorf("decode restore dir: %w", err)
	}

	return dirResp.Dir, nil
}

func (c *TransportClient) SendNetworkReconfig(oldIP, newIP string) error {
	nc := types.NetworkConfig{
		OldIP: oldIP,
		NewIP: newIP,
	}

	if err := writeMessage(c.conn, MsgNetworkReconfig, nc); err != nil {
		return fmt.Errorf("send network reconfig: %w", err)
	}

	resp, err := readMessage(c.conn)
	if err != nil {
		return fmt.Errorf("read network reconfig ack: %w", err)
	}

	if resp.Type == MsgError {
		var errResp struct{ Error string `json:"error"` }
		json.Unmarshal(resp.Payload, &errResp)
		return fmt.Errorf("network reconfig error: %s", errResp.Error)
	}

	return nil
}

func (c *TransportClient) SendComplete() error {
	return writeMessage(c.conn, MsgComplete, nil)
}

func sendDirectory(conn net.Conn, dir string, bwLimit int64, progress ProgressCallback, phase string) error {
	var totalSize int64
	filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil || info.IsDir() {
			return err
		}
		relPath, _ := filepath.Rel(dir, path)
		totalSize += info.Size()
		_ = relPath
		return nil
	})

	if progress != nil {
		progress(&types.ProgressState{
			Phase:      phase,
			TotalBytes: totalSize,
			StartTime:  time.Now(),
		})
	}

	var transferred int64
	var mu sync.Mutex
	startTime := time.Now()

	err := filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil || info.IsDir() {
			return err
		}

		relPath, _ := filepath.Rel(dir, path)
		header := map[string]interface{}{
			"name": relPath,
			"size": info.Size(),
		}
		headerData, _ := json.Marshal(header)
		headerLen := make([]byte, 8)
		binaryPutUint64(headerLen, uint64(len(headerData)))

		if _, err := conn.Write(headerLen); err != nil {
			return fmt.Errorf("write header len: %w", err)
		}
		if _, err := conn.Write(headerData); err != nil {
			return fmt.Errorf("write header: %w", err)
		}

		f, err := os.Open(path)
		if err != nil {
			return fmt.Errorf("open file %s: %w", path, err)
		}
		defer f.Close()

		reader := io.Reader(f)
		if bwLimit > 0 {
			reader = newBandwidthReader(f, bwLimit)
		}

		buf := make([]byte, 64*1024)
		for {
			n, rerr := reader.Read(buf)
			if n > 0 {
				if _, werr := conn.Write(buf[:n]); werr != nil {
					return fmt.Errorf("write file data: %w", werr)
				}

				mu.Lock()
				transferred += int64(n)
				if progress != nil {
					elapsed := time.Since(startTime).Seconds()
					speed := 0.0
					if elapsed > 0 {
						speed = float64(transferred) / elapsed / (1024 * 1024)
					}
					progress(&types.ProgressState{
						Phase:       phase,
						TotalBytes:  totalSize,
						Transferred: transferred,
						Speed:       speed,
						Percent:     float64(transferred) / float64(totalSize) * 100,
						StartTime:   startTime,
					})
				}
				mu.Unlock()
			}
			if rerr == io.EOF {
				break
			}
			if rerr != nil {
				return fmt.Errorf("read file: %w", rerr)
			}
		}

		return nil
	})

	if err != nil {
		return err
	}

	endHeader := map[string]interface{}{"name": "", "size": 0}
	endHeaderData, _ := json.Marshal(endHeader)
	endHeaderLen := make([]byte, 8)
	binaryPutUint64(endHeaderLen, uint64(len(endHeaderData)))
	if _, werr := conn.Write(endHeaderLen); werr != nil {
		return fmt.Errorf("write end header len: %w", werr)
	}
	if _, werr := conn.Write(endHeaderData); werr != nil {
		return fmt.Errorf("write end header: %w", werr)
	}

	return nil
}

func receiveFile(conn net.Conn, dir string, subdir string, bwLimit int64) error {
	targetDir := filepath.Join(dir, subdir)
	if err := os.MkdirAll(targetDir, 0700); err != nil {
		return err
	}

	for {
		headerLenBuf := make([]byte, 8)
		_, err := io.ReadFull(conn, headerLenBuf)
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return fmt.Errorf("read header len: %w", err)
		}

		headerLen := binaryGetUint64(headerLenBuf)
		headerBuf := make([]byte, headerLen)
		if _, err := io.ReadFull(conn, headerBuf); err != nil {
			return fmt.Errorf("read header: %w", err)
		}

		var header struct {
			Name string `json:"name"`
			Size int64  `json:"size"`
		}
		if err := json.Unmarshal(headerBuf, &header); err != nil {
			return fmt.Errorf("unmarshal header: %w", err)
		}

		if header.Name == "" {
			return nil
		}

		targetPath := filepath.Join(targetDir, header.Name)
		if err := os.MkdirAll(filepath.Dir(targetPath), 0700); err != nil {
			return fmt.Errorf("create dir: %w", err)
		}

		f, err := os.Create(targetPath)
		if err != nil {
			return fmt.Errorf("create file %s: %w", targetPath, err)
		}

		reader := io.Reader(conn)
		if bwLimit > 0 {
			reader = newBandwidthReader(conn, bwLimit)
		}

		written, err := io.CopyN(f, reader, header.Size)
		f.Close()

		if err != nil {
			return fmt.Errorf("write file %s: %w (written %d of %d)", targetPath, err, written, header.Size)
		}
	}
}

type bandwidthReader struct {
	reader    io.Reader
	bwLimit   int64
	lastRead  time.Time
	bytesRead int64
}

func newBandwidthReader(r io.Reader, bwLimit int64) *bandwidthReader {
	return &bandwidthReader{
		reader:   r,
		bwLimit:  bwLimit,
		lastRead: time.Now(),
	}
}

func (b *bandwidthReader) Read(p []byte) (int, error) {
	elapsed := time.Since(b.lastRead).Seconds()
	if elapsed < 0.1 {
		expectedBytes := int64(elapsed * float64(b.bwLimit) * 1024 * 1024)
		if b.bytesRead >= expectedBytes && expectedBytes > 0 {
			sleepTime := time.Duration((float64(b.bytesRead)/float64(b.bwLimit*1024*1024) - elapsed) * 1e9)
			if sleepTime > 0 {
				time.Sleep(sleepTime)
			}
		}
	}

	if time.Since(b.lastRead).Seconds() >= 1.0 {
		b.lastRead = time.Now()
		atomic.StoreInt64(&b.bytesRead, 0)
	}

	n, err := b.reader.Read(p)
	atomic.AddInt64(&b.bytesRead, int64(n))
	return n, err
}

func binaryPutUint64(buf []byte, val uint64) {
	for i := 0; i < 8; i++ {
		buf[i] = byte(val >> (56 - i*8))
	}
}

func binaryGetUint64(buf []byte) uint64 {
	var val uint64
	for i := 0; i < 8; i++ {
		val |= uint64(buf[i]) << (56 - i*8)
	}
	return val
}
