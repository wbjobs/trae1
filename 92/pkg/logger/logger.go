package logger

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"
)

type LogLevel int

const (
	LevelDebug LogLevel = iota
	LevelInfo
	LevelWarn
	LevelError
	LevelEvent
)

type LogEntry struct {
	Timestamp  time.Time              `json:"timestamp"`
	Level      string                 `json:"level"`
	Message    string                 `json:"message"`
	EventType  string                 `json:"event_type,omitempty"`
	Details    map[string]interface{} `json:"details,omitempty"`
}

type Logger struct {
	mu         sync.Mutex
	logFile    *os.File
	logDir     string
	minLevel   LogLevel
	showStdout bool
}

var (
	defaultLogger *Logger
	once          sync.Once
)

func Init(logDir string, level LogLevel) (*Logger, error) {
	var initErr error
	once.Do(func() {
		if err := os.MkdirAll(logDir, 0755); err != nil {
			initErr = fmt.Errorf("failed to create log directory: %w", err)
			return
		}

		logFileName := fmt.Sprintf("io-qos-%s.log", time.Now().Format("2006-01-02"))
		logFilePath := filepath.Join(logDir, logFileName)

		f, err := os.OpenFile(logFilePath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			initErr = fmt.Errorf("failed to open log file: %w", err)
			return
		}

		defaultLogger = &Logger{
			logFile:    f,
			logDir:     logDir,
			minLevel:   level,
			showStdout: true,
		}
	})

	return defaultLogger, initErr
}

func GetDefault() *Logger {
	if defaultLogger == nil {
		once.Do(func() {
			defaultLogger = &Logger{
				logFile:    nil,
				logDir:     ".",
				minLevel:   LevelInfo,
				showStdout: true,
			}
		})
	}
	return defaultLogger
}

func (l *Logger) SetLevel(level LogLevel) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.minLevel = level
}

func (l *Logger) SetShowStdout(show bool) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.showStdout = show
}

func (l *Logger) log(level LogLevel, levelStr string, format string, args ...interface{}) {
	if level < l.minLevel {
		return
	}

	msg := fmt.Sprintf(format, args...)
	entry := LogEntry{
		Timestamp: time.Now(),
		Level:     levelStr,
		Message:   msg,
	}

	l.writeEntry(entry)
}

func (l *Logger) logEvent(eventType string, details map[string]interface{}) {
	entry := LogEntry{
		Timestamp: time.Now(),
		Level:     "EVENT",
		EventType: eventType,
		Details:   details,
	}

	l.writeEntry(entry)
}

func (l *Logger) writeEntry(entry LogEntry) {
	l.mu.Lock()
	defer l.mu.Unlock()

	jsonData, err := json.Marshal(entry)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to marshal log entry: %v\n", err)
		return
	}

	logLine := string(jsonData) + "\n"

	if l.showStdout {
		fmt.Print(logLine)
	}

	if l.logFile != nil {
		if _, err := l.logFile.WriteString(logLine); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to write log entry: %v\n", err)
		}
	}
}

func (l *Logger) Close() error {
	l.mu.Lock()
	defer l.mu.Unlock()

	if l.logFile != nil {
		return l.logFile.Close()
	}
	return nil
}

func (l *Logger) Debug(format string, args ...interface{}) {
	l.log(LevelDebug, "DEBUG", format, args...)
}

func (l *Logger) Info(format string, args ...interface{}) {
	l.log(LevelInfo, "INFO", format, args...)
}

func (l *Logger) Warn(format string, args ...interface{}) {
	l.log(LevelWarn, "WARN", format, args...)
}

func (l *Logger) Error(format string, args ...interface{}) {
	l.log(LevelError, "ERROR", format, args...)
}

func (l *Logger) Event(eventType string, details map[string]interface{}) {
	l.logEvent(eventType, details)
}

func Debug(format string, args ...interface{}) {
	GetDefault().Debug(format, args...)
}

func Info(format string, args ...interface{}) {
	GetDefault().Info(format, args...)
}

func Warn(format string, args ...interface{}) {
	GetDefault().Warn(format, args...)
}

func Error(format string, args ...interface{}) {
	GetDefault().Error(format, args...)
}

func Event(eventType string, details map[string]interface{}) {
	GetDefault().Event(eventType, details)
}

func ParseLevel(levelStr string) LogLevel {
	switch levelStr {
	case "debug", "DEBUG":
		return LevelDebug
	case "info", "INFO":
		return LevelInfo
	case "warn", "WARN", "warning", "WARNING":
		return LevelWarn
	case "error", "ERROR":
		return LevelError
	default:
		return LevelInfo
	}
}

func (l LogLevel) String() string {
	switch l {
	case LevelDebug:
		return "DEBUG"
	case LevelInfo:
		return "INFO"
	case LevelWarn:
		return "WARN"
	case LevelError:
		return "ERROR"
	case LevelEvent:
		return "EVENT"
	default:
		return "UNKNOWN"
	}
}
