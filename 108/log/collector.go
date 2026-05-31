package log

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"sync"
	"time"

	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
)

type LogCollector struct {
	buffer     *bytes.Buffer
	mu         sync.Mutex
	logger     *zap.Logger
	instanceID string
}

func NewLogCollector(instanceID string, logger *zap.Logger) *LogCollector {
	return &LogCollector{
		buffer:     &bytes.Buffer{},
		logger:     logger,
		instanceID: instanceID,
	}
}

func (lc *LogCollector) Write(p []byte) (n int, err error) {
	lc.mu.Lock()
	defer lc.mu.Unlock()

	lc.buffer.Write(p)

	lines := bytes.Split(p, []byte{'\n'})
	for _, line := range lines {
		if len(line) > 0 {
			lc.logger.Info("wasm_log",
				zap.String("instance_id", lc.instanceID),
				zap.ByteString("log", line),
			)
		}
	}

	return len(p), nil
}

func (lc *LogCollector) GetLogs() string {
	lc.mu.Lock()
	defer lc.mu.Unlock()
	return lc.buffer.String()
}

func (lc *LogCollector) Flush() string {
	lc.mu.Lock()
	defer lc.mu.Unlock()
	logs := lc.buffer.String()
	lc.buffer.Reset()
	return logs
}

type SlowLogEntry struct {
	Timestamp     time.Time `json:"timestamp"`
	InstanceID    string    `json:"instance_id"`
	ServiceName   string    `json:"service_name"`
	Duration      time.Duration `json:"duration"`
	StatusCode    int       `json:"status_code"`
	Path          string    `json:"path"`
	Method        string    `json:"method"`
}

type SlowLogger struct {
	mu         sync.RWMutex
	entries    []SlowLogEntry
	maxEntries int
	file       *os.File
	logger     *zap.Logger
}

func NewSlowLogger(filePath string, maxEntries int, logger *zap.Logger) (*SlowLogger, error) {
	file, err := os.OpenFile(filePath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		return nil, err
	}

	return &SlowLogger{
		entries:    make([]SlowLogEntry, 0, maxEntries),
		maxEntries: maxEntries,
		file:       file,
		logger:     logger,
	}, nil
}

func (sl *SlowLogger) Log(entry SlowLogEntry) {
	sl.mu.Lock()
	defer sl.mu.Unlock()

	sl.entries = append(sl.entries, entry)
	if len(sl.entries) > sl.maxEntries {
		sl.entries = sl.entries[1:]
	}

	sl.logger.Warn("slow_request_logged",
		zap.String("instance_id", entry.InstanceID),
		zap.String("service_name", entry.ServiceName),
		zap.Duration("duration", entry.Duration),
		zap.Int("status_code", entry.StatusCode),
		zap.String("path", entry.Path),
		zap.String("method", entry.Method),
	)

	if sl.file != nil {
		jsonLine := fmt.Sprintf(`{"timestamp":"%s","instance_id":"%s","service_name":"%s","duration_ms":%d,"status_code":%d,"path":"%s","method":"%s"}`+"\n",
			entry.Timestamp.Format(time.RFC3339),
			entry.InstanceID,
			entry.ServiceName,
			entry.Duration.Milliseconds(),
			entry.StatusCode,
			entry.Path,
			entry.Method,
		)
		sl.file.WriteString(jsonLine)
	}
}

func (sl *SlowLogger) GetEntries() []SlowLogEntry {
	sl.mu.RLock()
	defer sl.mu.RUnlock()
	result := make([]SlowLogEntry, len(sl.entries))
	copy(result, sl.entries)
	return result
}

func (sl *SlowLogger) Close() error {
	if sl.file != nil {
		return sl.file.Close()
	}
	return nil
}

type BufferedLogWriter struct {
	encoder zapcore.Encoder
	writer  io.Writer
	mu      sync.Mutex
}

func NewBufferedLogWriter(w io.Writer) *BufferedLogWriter {
	encoderConfig := zapcore.EncoderConfig{
		TimeKey:        "timestamp",
		LevelKey:       "level",
		NameKey:        "logger",
		CallerKey:      "caller",
		FunctionKey:    zapcore.OmitKey,
		MessageKey:     "message",
		StacktraceKey:  "stacktrace",
		LineEnding:     zapcore.DefaultLineEnding,
		EncodeLevel:    zapcore.LowercaseLevelEncoder,
		EncodeTime:     zapcore.ISO8601TimeEncoder,
		EncodeDuration: zapcore.SecondsDurationEncoder,
		EncodeCaller:   zapcore.ShortCallerEncoder,
	}

	return &BufferedLogWriter{
		encoder: zapcore.NewJSONEncoder(encoderConfig),
		writer:  w,
	}
}

func (w *BufferedLogWriter) Write(p []byte) (n int, err error) {
	w.mu.Lock()
	defer w.mu.Unlock()

	lines := bytes.Split(p, []byte{'\n'})
	for _, line := range lines {
		if len(line) > 0 {
			w.writer.Write(line)
			w.writer.Write([]byte{'\n'})
		}
	}

	return len(p), nil
}

func InitLogger(level string) (*zap.Logger, error) {
	var zapLevel zapcore.Level
	switch level {
	case "debug":
		zapLevel = zapcore.DebugLevel
	case "info":
		zapLevel = zapcore.InfoLevel
	case "warn":
		zapLevel = zapcore.WarnLevel
	case "error":
		zapLevel = zapcore.ErrorLevel
	default:
		zapLevel = zapcore.InfoLevel
	}

	config := zap.Config{
		Level:            zap.NewAtomicLevelAt(zapLevel),
		Development:      false,
		Encoding:        "json",
		EncoderConfig:   zap.NewProductionEncoderConfig(),
		OutputPaths:     []string{"stderr"},
		ErrorOutputPaths: []string{"stderr"},
	}

	logger, err := config.Build()
	if err != nil {
		return nil, err
	}

	return logger, nil
}

type LogRotation struct {
	maxSize    int
	maxAge     int
	buffers    map[string]*bytes.Buffer
	mu         sync.RWMutex
	logger     *zap.Logger
}

func NewLogRotation(maxSize int, maxAge int, logger *zap.Logger) *LogRotation {
	return &LogRotation{
		maxSize: maxSize,
		maxAge:  maxAge,
		buffers: make(map[string]*bytes.Buffer),
		logger:  logger,
	}
}

func (lr *LogRotation) Collect(instanceID string, data []byte) {
	lr.mu.Lock()
	defer lr.mu.Unlock()

	if _, ok := lr.buffers[instanceID]; !ok {
		lr.buffers[instanceID] = &bytes.Buffer{}
	}

	lr.buffers[instanceID].Write(data)

	if lr.buffers[instanceID].Len() >= lr.maxSize {
		lr.flushBuffer(instanceID)
	}
}

func (lr *LogRotation) flushBuffer(instanceID string) {
	if buf, ok := lr.buffers[instanceID]; ok {
		lr.logger.Info("log_rotation",
			zap.String("instance_id", instanceID),
			zap.Int("size", buf.Len()),
		)
		buf.Reset()
	}
}

func (lr *LogRotation) FlushAll() {
	lr.mu.Lock()
	defer lr.mu.Unlock()

	for id := range lr.buffers {
		lr.flushBuffer(id)
	}
}

func (lr *LogRotation) Start(interval time.Duration) {
	ticker := time.NewTicker(interval)
	go func() {
		for range ticker.C {
			lr.FlushAll()
		}
	}()
}
