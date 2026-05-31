package executor

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	"go.uber.org/zap"

	"github.com/sandbox/executor/internal/aisecurity"
	"github.com/sandbox/executor/internal/config"
	"github.com/sandbox/executor/internal/sandbox"
	"github.com/sandbox/executor/internal/stats"
)

type Command struct {
	Name      string   `json:"name"`
	Args      []string `json:"args"`
	InputFiles []InputFile `json:"input_files,omitempty"`
	Timeout   *int     `json:"timeout,omitempty"`
}

type InputFile struct {
	Name    string `json:"name"`
	Content string `json:"content"`
}

type ExecutionResponse struct {
	RequestID     string               `json:"request_id"`
	Status        string               `json:"status"`
	ExitCode      int                  `json:"exit_code"`
	Stdout        string               `json:"stdout"`
	Stderr        string               `json:"stderr"`
	ExecutionTime time.Duration        `json:"execution_time"`
	OutputFiles   []OutputFile         `json:"output_files,omitempty"`
	Stats         *ResourceUsage       `json:"stats,omitempty"`
	CreatedAt     time.Time            `json:"created_at"`
	FinishedAt    *time.Time           `json:"finished_at,omitempty"`
	Error         string               `json:"error,omitempty"`
}

type OutputFile struct {
	Name string `json:"name"`
	Size int64  `json:"size"`
	Path string `json:"path"`
}

type ResourceUsage struct {
	CPUTotal    float64 `json:"cpu_total_seconds"`
	CPUPercent  float64 `json:"cpu_percent"`
	MemoryUsed  uint64  `json:"memory_used_bytes"`
	MemoryMax   uint64  `json:"memory_max_bytes"`
	IOReadBytes uint64  `json:"io_read_bytes"`
	IOWriteBytes uint64 `json:"io_write_bytes"`
	IOReadRate  float64 `json:"io_read_rate_bytes_per_sec"`
	IOWriteRate float64 `json:"io_write_rate_bytes_per_sec"`
}

type Executor struct {
	cfg            *config.Config
	logger         *zap.Logger
	sandboxManager *sandbox.Manager
	securityChecker *aisecurity.SecurityChecker
	allowedCmds    map[string]bool
	mu             sync.RWMutex
}

func NewExecutor(cfg *config.Config, logger *zap.Logger, sandboxManager *sandbox.Manager, securityChecker *aisecurity.SecurityChecker) *Executor {
	allowedCmds := map[string]bool{
		"ffmpeg":      true,
		"ffprobe":     true,
		"convert":     true,
		"identify":    true,
		"mogrify":     true,
		"composite":   true,
		"montage":     true,
		"compare":     true,
		"stream":      true,
		"display":     true,
		"import":      true,
		"conjure":     true,
		"pdftotext":   true,
		"pdfinfo":     true,
		"pdftoppm":    true,
		"pdftohtml":   true,
		"pdfimages":   true,
		"pdffonts":    true,
		"gs":          true,
		"magick":      true,
	}

	return &Executor{
		cfg:             cfg,
		logger:          logger,
		sandboxManager:  sandboxManager,
		securityChecker: securityChecker,
		allowedCmds:     allowedCmds,
	}
}

func (e *Executor) Execute(ctx context.Context, cmd *Command) (*ExecutionResponse, error) {
	if err := e.validateCommand(cmd); err != nil {
		return nil, fmt.Errorf("command validation failed: %w", err)
	}

	requestID := generateRequestID()

	aiResult, err := e.securityChecker.PreCheck(ctx, requestID, cmd.Name, cmd.Args)
	if err != nil {
		e.logger.Warn("AI security pre-check error, proceeding with caution",
			zap.Error(err),
		)
	}

	if aiResult != nil {
		switch aiResult.Decision {
		case aisecurity.DecisionReject:
			return &ExecutionResponse{
				RequestID: requestID,
				Status:    "rejected",
				Error:     fmt.Sprintf("AI security check rejected this command (score: %d/100). Reasons: %s", aiResult.Score, strings.Join(aiResult.Reasons, "; ")),
			}, nil

		case aisecurity.DecisionReview:
			resp := &ExecutionResponse{
				RequestID: aiResult.ApprovalID,
				Status:    "pending_review",
				Error:     fmt.Sprintf("Command requires manual approval (score: %d/100). Approval ID: %s. Reasons: %s", aiResult.Score, aiResult.ApprovalID, strings.Join(aiResult.Reasons, "; ")),
			}
			if aiResult.ApprovalID != "" {
				approved, waitErr := e.securityChecker.WaitForApproval(ctx, aiResult.ApprovalID)
				if waitErr != nil {
					resp.Status = "approval_timeout"
					resp.Error = fmt.Sprintf("Approval timeout: %v", waitErr)
					return resp, nil
				}
				if !approved {
					resp.Status = "rejected"
					resp.Error = "Command was rejected during manual review"
					return resp, nil
				}
			}
		}

		e.logger.Info("AI security pre-check passed",
			zap.Int("score", aiResult.Score),
			zap.String("decision", string(aiResult.Decision)),
			zap.Bool("cached", aiResult.Cached),
		)
	}

	sb, err := e.sandboxManager.CreateSandbox(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to create sandbox: %w", err)
	}

	resp := &ExecutionResponse{
		RequestID: sb.ID,
		Status:    string(sandbox.SandboxStatusPending),
		CreatedAt: sb.CreatedAt,
	}

	if err := e.prepareInputFiles(sb, cmd.InputFiles); err != nil {
		e.sandboxManager.DestroySandbox(sb)
		return nil, fmt.Errorf("failed to prepare input files: %w", err)
	}

	timeout := e.cfg.Sandbox.DefaultTimeout
	if cmd.Timeout != nil && *cmd.Timeout > 0 {
		customTimeout := time.Duration(*cmd.Timeout) * time.Second
		if customTimeout < timeout {
			timeout = customTimeout
		}
	}

	result, err := e.sandboxManager.Execute(ctx, sb, cmd.Name, cmd.Args, timeout)

	resp.Status = string(sb.GetStatus())

	if err != nil {
		resp.Error = err.Error()
		e.sandboxManager.DestroySandbox(sb)
		return resp, nil
	}

	resp.ExitCode = result.ExitCode
	resp.Stdout = result.Stdout
	resp.Stderr = result.Stderr
	resp.ExecutionTime = result.ExecutionTime
	resp.FinishedAt = sb.FinishedAt

	if result.Stats != nil {
		resp.Stats = e.convertStats(result.Stats)
	}

	outputFiles, err := e.collectOutputFiles(sb)
	if err != nil {
		e.logger.Warn("Failed to collect output files", zap.Error(err))
	}
	resp.OutputFiles = outputFiles

	e.sandboxManager.DestroySandbox(sb)

	return resp, nil
}

func (e *Executor) ExecuteAsync(ctx context.Context, cmd *Command) (*ExecutionResponse, error) {
	if err := e.validateCommand(cmd); err != nil {
		return nil, fmt.Errorf("command validation failed: %w", err)
	}

	sb, err := e.sandboxManager.CreateSandbox(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to create sandbox: %w", err)
	}

	resp := &ExecutionResponse{
		RequestID: sb.ID,
		Status:    string(sandbox.SandboxStatusPending),
		CreatedAt: sb.CreatedAt,
	}

	go func() {
		asyncCtx := context.Background()

		if err := e.prepareInputFiles(sb, cmd.InputFiles); err != nil {
			e.logger.Error("Failed to prepare input files for async execution",
				zap.String("sandbox_id", sb.ID),
				zap.Error(err),
			)
			e.sandboxManager.DestroySandbox(sb)
			return
		}

		timeout := e.cfg.Sandbox.DefaultTimeout
		if cmd.Timeout != nil && *cmd.Timeout > 0 {
			customTimeout := time.Duration(*cmd.Timeout) * time.Second
			if customTimeout < timeout {
				timeout = customTimeout
			}
		}

		result, err := e.sandboxManager.Execute(asyncCtx, sb, cmd.Name, cmd.Args, timeout)
		if err != nil {
			e.logger.Error("Async execution failed",
				zap.String("sandbox_id", sb.ID),
				zap.Error(err),
			)
		}

		_ = result

		e.sandboxManager.DestroySandbox(sb)
	}()

	return resp, nil
}

func (e *Executor) GetStatus(requestID string) (*ExecutionResponse, error) {
	sb, ok := e.sandboxManager.GetSandbox(requestID)
	if !ok {
		return nil, fmt.Errorf("request not found: %s", requestID)
	}

	resp := &ExecutionResponse{
		RequestID:  sb.ID,
		Status:     string(sb.GetStatus()),
		CreatedAt:  sb.CreatedAt,
		FinishedAt: sb.FinishedAt,
	}

	if stats := sb.GetStats(); stats != nil {
		resp.Stats = e.convertStats(stats)
	}

	return resp, nil
}

func (e *Executor) validateCommand(cmd *Command) error {
	if cmd == nil {
		return fmt.Errorf("command cannot be nil")
	}

	if cmd.Name == "" {
		return fmt.Errorf("command name cannot be empty")
	}

	baseName := filepath.Base(cmd.Name)
	if !e.allowedCmds[baseName] {
		return fmt.Errorf("command not allowed: %s", cmd.Name)
	}

	fullCmd := cmd.Name + " " + strings.Join(cmd.Args, " ")
	if err := e.detectForkBomb(fullCmd); err != nil {
		return err
	}

	for _, arg := range cmd.Args {
		if strings.Contains(arg, "..") {
			return fmt.Errorf("path traversal detected in argument: %s", arg)
		}
		if strings.Contains(arg, "|") || strings.Contains(arg, "&&") || strings.Contains(arg, ";") {
			return fmt.Errorf("shell injection detected in argument: %s", arg)
		}
		if strings.Contains(arg, "$") || strings.Contains(arg, "`") {
			return fmt.Errorf("shell expansion detected in argument: %s", arg)
		}
		if err := e.detectForkBomb(arg); err != nil {
			return err
		}
	}

	return nil
}

func (e *Executor) detectForkBomb(input string) error {
	lower := strings.ToLower(input)

	forkBombPatterns := []string{
		":(){ :|:& };:",
		":(){ :|: & };:",
		":(){ :|:&};:",
		":(){ :|: &};:",
		"bomb() { bomb | bomb & }; bomb",
		"bomb(){bomb|bomb&};bomb",
		"forkbomb",
		"fork_bomb",
		".(){ .|.& };.",
		"$0|$0&",
	}

	for _, pattern := range forkBombPatterns {
		if strings.Contains(lower, pattern) {
			return fmt.Errorf("fork bomb pattern detected: %s", pattern)
		}
	}

	forkBombRegexPatterns := []string{
		`nohup.*sh.*-c.*&`,
		`setsid.*sh.*-c`,
		`disown.*-r`,
	}

	for _, pattern := range forkBombRegexPatterns {
		matched, _ := regexp.MatchString(pattern, lower)
		if matched {
			return fmt.Errorf("fork bomb pattern detected: %s", pattern)
		}
	}

	backgroundProcessPatterns := []string{
		"&>/dev/null &",
		">/dev/null 2>&1 &",
		"& |",
		"| &",
	}

	for _, pattern := range backgroundProcessPatterns {
		if strings.Contains(lower, pattern) {
			return fmt.Errorf("suspicious background process pattern detected: %s", pattern)
		}
	}

	dangerousOps := []string{
		"kill -9 -1",
		"rm -rf /",
		"mkfs.",
		"dd if=",
		">/dev/sda",
	}

	for _, pattern := range dangerousOps {
		if strings.Contains(lower, pattern) {
			return fmt.Errorf("dangerous operation detected: %s", pattern)
		}
	}

	pidExhaustionPatterns := []string{
		"while true",
		"for ((;;))",
		"for((;;))",
		"until false",
		"exec $0",
		"exec $SHELL",
		"exec bash",
		"exec sh",
		"exec zsh",
	}

	for _, pattern := range pidExhaustionPatterns {
		if strings.Contains(lower, pattern) {
			return fmt.Errorf("PID exhaustion pattern detected: %s", pattern)
		}
	}

	return nil
}

func (e *Executor) prepareInputFiles(sb *sandbox.Sandbox, inputFiles []InputFile) error {
	for _, f := range inputFiles {
		fileName := filepath.Base(f.Name)
		if fileName != f.Name {
			return fmt.Errorf("invalid file name (path traversal): %s", f.Name)
		}

		filePath := filepath.Join(sb.InputDir, fileName)
		if err := os.WriteFile(filePath, []byte(f.Content), 0640); err != nil {
			return fmt.Errorf("failed to write input file %s: %w", fileName, err)
		}

		e.logger.Debug("Input file prepared",
			zap.String("sandbox_id", sb.ID),
			zap.String("file_name", fileName),
			zap.Int("size", len(f.Content)),
		)
	}

	return nil
}

func (e *Executor) collectOutputFiles(sb *sandbox.Sandbox) ([]OutputFile, error) {
	var outputFiles []OutputFile

	err := filepath.Walk(sb.OutputDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}

		relPath, err := filepath.Rel(sb.OutputDir, path)
		if err != nil {
			return err
		}

		outputFiles = append(outputFiles, OutputFile{
			Name: relPath,
			Size: info.Size(),
			Path: path,
		})

		return nil
	})

	return outputFiles, err
}

func (e *Executor) convertStats(s *stats.ResourceStats) *ResourceUsage {
	if s == nil {
		return nil
	}

	return &ResourceUsage{
		CPUTotal:    s.CPUTotal,
		CPUPercent:  s.CPUPercent,
		MemoryUsed:  s.MemoryUsed,
		MemoryMax:   s.MemoryMax,
		IOReadBytes: s.IOReadBytes,
		IOWriteBytes: s.IOWriteBytes,
		IOReadRate:  s.IOReadRate,
		IOWriteRate: s.IOWriteRate,
	}
}

func generateRequestID() string {
	return uuid.New().String()
}
