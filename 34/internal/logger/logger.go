package logger

import (
	"os"

	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
)

var L *zap.Logger
var S *zap.SugaredLogger

func Init(level string) {
	atomicLevel := zap.NewAtomicLevel()
	switch level {
	case "debug":
		atomicLevel.SetLevel(zapcore.DebugLevel)
	case "warn", "warning":
		atomicLevel.SetLevel(zapcore.WarnLevel)
	case "error":
		atomicLevel.SetLevel(zapcore.ErrorLevel)
	default:
		atomicLevel.SetLevel(zapcore.InfoLevel)
	}

	cfg := zap.NewProductionConfig()
	cfg.Level = atomicLevel
	cfg.Encoding = "console"
	cfg.EncoderConfig.EncodeTime = zapcore.ISO8601TimeEncoder
	cfg.EncoderConfig.EncodeLevel = zapcore.CapitalLevelEncoder
	cfg.OutputPaths = []string{"stdout"}
	cfg.ErrorOutputPaths = []string{"stderr"}

	l, err := cfg.Build()
	if err != nil {
		l = zap.New(zapcore.NewCore(
			zapcore.NewConsoleEncoder(zap.NewDevelopmentEncoderConfig()),
			zapcore.Lock(os.Stdout),
			atomicLevel,
		))
	}
	L = l
	S = l.Sugar()
}

func Sync() {
	if L != nil {
		_ = L.Sync()
	}
}
