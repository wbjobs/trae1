package transfer

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"time"
)

type TransferOptions struct {
	Bwlimit    int
	ProgressCb func(transferred int64, total int64, speed float64)
}

type TransferHeader struct {
	ContainerName string `json:"container_name"`
	TotalSize     int64  `json:"total_size"`
	FileCount     int    `json:"file_count"`
	Timestamp     int64  `json:"timestamp"`
}

type FileHeader struct {
	Path string `json:"path"`
	Size int64  `json:"size"`
	Mode uint32 `json:"mode"`
}

type RestoreCommand struct {
	Action        string `json:"action"`
	ContainerName string `json:"container_name"`
	Directory     string `json:"directory"`
}

func ConnectTarget(host string, port int) (net.Conn, error) {
	addr := fmt.Sprintf("%s:%d", host, port)
	conn, err := net.DialTimeout("tcp", addr, 30*time.Second)
	if err != nil {
		return nil, fmt.Errorf("连接目标主机失败: %w", err)
	}
	return conn, nil
}

func SendHeader(conn net.Conn, header *TransferHeader) error {
	data, err := json.Marshal(header)
	if err != nil {
		return err
	}

	lenBuf := make([]byte, 4)
	binary.BigEndian.PutUint32(lenBuf, uint32(len(data)))

	if _, err := conn.Write(lenBuf); err != nil {
		return err
	}
	if _, err := conn.Write(data); err != nil {
		return err
	}
	return nil
}

func ReceiveHeader(reader io.Reader) (string, error) {
	lenBuf := make([]byte, 4)
	if _, err := io.ReadFull(reader, lenBuf); err != nil {
		return "", fmt.Errorf("读取头部长度失败: %w", err)
	}

	headerLen := binary.BigEndian.Uint32(lenBuf)
	headerBuf := make([]byte, headerLen)
	if _, err := io.ReadFull(reader, headerBuf); err != nil {
		return "", fmt.Errorf("读取头部数据失败: %w", err)
	}

	var header TransferHeader
	if err := json.Unmarshal(headerBuf, &header); err != nil {
		return "", fmt.Errorf("解析头部失败: %w", err)
	}

	return header.ContainerName, nil
}

func SendDirectory(conn net.Conn, dir string, opts TransferOptions) (int64, error) {
	var totalBytes int64
	var fileCount int

	filesInfo, err := getFilesInfo(dir)
	if err != nil {
		return 0, err
	}

	for _, info := range filesInfo {
		totalBytes += info.Size
		fileCount++
	}

	header := &TransferHeader{
		ContainerName: filepath.Base(dir),
		TotalSize:     totalBytes,
		FileCount:     fileCount,
		Timestamp:     time.Now().Unix(),
	}

	if err := SendHeader(conn, header); err != nil {
		return 0, fmt.Errorf("发送头部失败: %w", err)
	}

	var transferred int64
	startTime := time.Now()

	for _, info := range filesInfo {
		relPath, err := filepath.Rel(dir, info.Path)
		if err != nil {
			return transferred, err
		}

		fileHeader := &FileHeader{
			Path: relPath,
			Size: info.Size,
			Mode: uint32(info.Mode),
		}

		if err := sendFileHeader(conn, fileHeader); err != nil {
			return transferred, fmt.Errorf("发送文件头失败: %w", err)
		}

		bytes, err := sendFileContent(conn, info.Path, info.Size, opts, startTime, &transferred, totalBytes)
		if err != nil {
			return transferred, fmt.Errorf("发送文件失败: %w", err)
		}
		transferred += bytes
	}

	return transferred, nil
}

func ReceiveDirectory(reader io.Reader, dir string) error {
	lenBuf := make([]byte, 4)
	if _, err := io.ReadFull(reader, lenBuf); err != nil {
		return fmt.Errorf("读取头部长度失败: %w", err)
	}

	headerLen := binary.BigEndian.Uint32(lenBuf)
	headerBuf := make([]byte, headerLen)
	if _, err := io.ReadFull(reader, headerBuf); err != nil {
		return fmt.Errorf("读取头部数据失败: %w", err)
	}

	var header TransferHeader
	if err := json.Unmarshal(headerBuf, &header); err != nil {
		return fmt.Errorf("解析头部失败: %w", err)
	}

	var received int64
	for received < header.TotalSize {
		fileHeader, err := receiveFileHeader(reader)
		if err != nil {
			return fmt.Errorf("接收文件头失败: %w", err)
		}

		if err := receiveFileContent(reader, dir, fileHeader); err != nil {
			return fmt.Errorf("接收文件失败: %w", err)
		}
		received += fileHeader.Size
	}

	return nil
}

func SendRestoreCommand(conn net.Conn, containerName string) error {
	cmd := &RestoreCommand{
		Action:        "restore",
		ContainerName: containerName,
	}

	data, err := json.Marshal(cmd)
	if err != nil {
		return err
	}

	lenBuf := make([]byte, 4)
	binary.BigEndian.PutUint32(lenBuf, uint32(len(data)))

	if _, err := conn.Write(lenBuf); err != nil {
		return err
	}
	if _, err := conn.Write(data); err != nil {
		return err
	}

	return nil
}

func SendAck(conn net.Conn, success bool, message string) error {
	ack := map[string]interface{}{
		"success": success,
		"message": message,
	}
	data, err := json.Marshal(ack)
	if err != nil {
		return err
	}
	_, err = conn.Write(data)
	return err
}

func sendFileHeader(conn net.Conn, header *FileHeader) error {
	data, err := json.Marshal(header)
	if err != nil {
		return err
	}

	lenBuf := make([]byte, 4)
	binary.BigEndian.PutUint32(lenBuf, uint32(len(data)))

	if _, err := conn.Write(lenBuf); err != nil {
		return err
	}
	if _, err := conn.Write(data); err != nil {
		return err
	}
	return nil
}

func receiveFileHeader(reader io.Reader) (*FileHeader, error) {
	lenBuf := make([]byte, 4)
	if _, err := io.ReadFull(reader, lenBuf); err != nil {
		return nil, err
	}

	headerLen := binary.BigEndian.Uint32(lenBuf)
	headerBuf := make([]byte, headerLen)
	if _, err := io.ReadFull(reader, headerBuf); err != nil {
		return nil, err
	}

	var header FileHeader
	if err := json.Unmarshal(headerBuf, &header); err != nil {
		return nil, err
	}
	return &header, nil
}

func sendFileContent(conn net.Conn, path string, size int64, opts TransferOptions, startTime time.Time, transferred *int64, totalBytes int64) (int64, error) {
	file, err := os.Open(path)
	if err != nil {
		return 0, err
	}
	defer file.Close()

	buf := make([]byte, 64*1024)
	var sent int64

	for sent < size {
		remaining := size - sent
		toRead := int64(len(buf))
		if remaining < toRead {
			toRead = remaining
		}

		n, err := file.Read(buf[:toRead])
		if err != nil && err != io.EOF {
			return sent, err
		}
		if n == 0 {
			break
		}

		if _, err := conn.Write(buf[:n]); err != nil {
			return sent, err
		}

		sent += int64(n)
		*transferred += int64(n)

		if opts.Bwlimit > 0 {
			applyBandwidthLimit(opts.Bwlimit, startTime, *transferred)
		}

		if opts.ProgressCb != nil {
			elapsed := time.Since(startTime).Seconds()
			speed := float64(*transferred) / elapsed
			opts.ProgressCb(*transferred, totalBytes, speed)
		}
	}

	return sent, nil
}

func receiveFileContent(reader io.Reader, baseDir string, header *FileHeader) error {
	fullPath := filepath.Join(baseDir, header.Path)

	if err := os.MkdirAll(filepath.Dir(fullPath), 0755); err != nil {
		return err
	}

	file, err := os.Create(fullPath)
	if err != nil {
		return err
	}
	defer file.Close()

	buf := make([]byte, 64*1024)
	var received int64

	for received < header.Size {
		remaining := header.Size - received
		toRead := int64(len(buf))
		if remaining < toRead {
			toRead = remaining
		}

		n, err := io.ReadFull(reader, buf[:toRead])
		if err != nil {
			return err
		}

		if _, err := file.Write(buf[:n]); err != nil {
			return err
		}
		received += int64(n)
	}

	return os.Chmod(fullPath, os.FileMode(header.Mode))
}

func applyBandwidthLimit(bwlimitKBs int, startTime time.Time, transferred int64) {
	elapsed := time.Since(startTime).Seconds()
	expectedBytes := float64(bwlimitKBs) * 1024 * elapsed

	if float64(transferred) > expectedBytes {
		sleepTime := float64(transferred)/float64(bwlimitKBs*1024) - elapsed
		if sleepTime > 0 {
			time.Sleep(time.Duration(sleepTime * float64(time.Second)))
		}
	}
}

type fileInfo struct {
	Path string
	Size int64
	Mode os.FileMode
}

func getFilesInfo(dir string) ([]fileInfo, error) {
	var files []fileInfo

	err := filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		files = append(files, fileInfo{
			Path: path,
			Size: info.Size(),
			Mode: info.Mode(),
		})
		return nil
	})

	return files, err
}
