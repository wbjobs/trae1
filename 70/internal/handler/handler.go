package handler

import (
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"go.uber.org/zap"

	"github.com/sandbox/executor/internal/aisecurity"
	"github.com/sandbox/executor/internal/executor"
	"github.com/sandbox/executor/internal/sandbox"
)

type Handler struct {
	executor        *executor.Executor
	manager         *sandbox.Manager
	securityChecker *aisecurity.SecurityChecker
	logger          *zap.Logger
}

func NewHandler(exec *executor.Executor, manager *sandbox.Manager, securityChecker *aisecurity.SecurityChecker, logger *zap.Logger) *Handler {
	return &Handler{
		executor:        exec,
		manager:         manager,
		securityChecker: securityChecker,
		logger:          logger,
	}
}

type ApprovalRequest struct {
	Approved bool   `json:"approved" binding:"required"`
	Reason   string `json:"reason"`
}

type PendingApprovalResponse struct {
	RequestID  string   `json:"request_id"`
	Command    string   `json:"command"`
	Args       []string `json:"args"`
	Score      int      `json:"score"`
	Reasons    []string `json:"reasons"`
	Category   string   `json:"category"`
	Status     string   `json:"status"`
	CreatedAt  string   `json:"created_at"`
	ExpiresAt  string   `json:"expires_at"`
}

type ExecuteRequest struct {
	Command    string              `json:"command" binding:"required"`
	Args       []string            `json:"args"`
	InputFiles []InputFileRequest  `json:"input_files,omitempty"`
	Timeout    *int                `json:"timeout,omitempty"`
}

type InputFileRequest struct {
	Name    string `json:"name" binding:"required"`
	Content string `json:"content" binding:"required"`
}

type ExecuteResponse struct {
	RequestID     string          `json:"request_id"`
	Status        string          `json:"status"`
	ExitCode      int             `json:"exit_code"`
	Stdout        string          `json:"stdout"`
	Stderr        string          `json:"stderr"`
	ExecutionTime float64         `json:"execution_time_seconds"`
	OutputFiles   []OutputFile    `json:"output_files,omitempty"`
	Stats         *ResourceStats  `json:"stats,omitempty"`
	CreatedAt     time.Time       `json:"created_at"`
	FinishedAt    *time.Time      `json:"finished_at,omitempty"`
	Error         string          `json:"error,omitempty"`
}

type OutputFile struct {
	Name string `json:"name"`
	Size int64  `json:"size"`
	Path string `json:"path"`
}

type ResourceStats struct {
	CPUTotal     float64 `json:"cpu_total_seconds"`
	CPUPercent   float64 `json:"cpu_percent"`
	MemoryUsed   uint64  `json:"memory_used_bytes"`
	MemoryMax    uint64  `json:"memory_max_bytes"`
	IOReadBytes  uint64  `json:"io_read_bytes"`
	IOWriteBytes uint64  `json:"io_write_bytes"`
	IOReadRate   float64 `json:"io_read_rate_bytes_per_sec"`
	IOWriteRate  float64 `json:"io_write_rate_bytes_per_sec"`
}

type StatusResponse struct {
	RequestID  string         `json:"request_id"`
	Status     string         `json:"status"`
	CreatedAt  time.Time      `json:"created_at"`
	FinishedAt *time.Time     `json:"finished_at,omitempty"`
	Stats      *ResourceStats `json:"stats,omitempty"`
}

type HealthResponse struct {
	Status    string `json:"status"`
	Timestamp string `json:"timestamp"`
}

func (h *Handler) RegisterRoutes(router *gin.Engine) {
	api := router.Group("/api/v1")
	{
		api.POST("/execute", h.Execute)
		api.POST("/execute/async", h.ExecuteAsync)
		api.GET("/status/:request_id", h.GetStatus)

		approvals := api.Group("/approvals")
		{
			approvals.GET("", h.ListPendingApprovals)
			approvals.GET("/:approval_id", h.GetApproval)
			approvals.POST("/:approval_id/resolve", h.ResolveApproval)
		}
	}

	router.GET("/health", h.Health)
}

func (h *Handler) Execute(c *gin.Context) {
	var req ExecuteRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		h.logger.Warn("Invalid execute request", zap.Error(err))
		c.JSON(http.StatusBadRequest, gin.H{
			"error": "invalid request: " + err.Error(),
		})
		return
	}

	cmd := &executor.Command{
		Name:    req.Command,
		Args:    req.Args,
		Timeout: req.Timeout,
	}

	for _, f := range req.InputFiles {
		cmd.InputFiles = append(cmd.InputFiles, executor.InputFile{
			Name:    f.Name,
			Content: f.Content,
		})
	}

	result, err := h.executor.Execute(c.Request.Context(), cmd)
	if err != nil {
		h.logger.Error("Execution failed",
			zap.String("command", req.Command),
			zap.Error(err),
		)
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": err.Error(),
		})
		return
	}

	resp := h.convertToResponse(result)
	c.JSON(http.StatusOK, resp)
}

func (h *Handler) ExecuteAsync(c *gin.Context) {
	var req ExecuteRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		h.logger.Warn("Invalid async execute request", zap.Error(err))
		c.JSON(http.StatusBadRequest, gin.H{
			"error": "invalid request: " + err.Error(),
		})
		return
	}

	cmd := &executor.Command{
		Name:    req.Command,
		Args:    req.Args,
		Timeout: req.Timeout,
	}

	for _, f := range req.InputFiles {
		cmd.InputFiles = append(cmd.InputFiles, executor.InputFile{
			Name:    f.Name,
			Content: f.Content,
		})
	}

	result, err := h.executor.ExecuteAsync(c.Request.Context(), cmd)
	if err != nil {
		h.logger.Error("Async execution failed",
			zap.String("command", req.Command),
			zap.Error(err),
		)
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": err.Error(),
		})
		return
	}

	c.JSON(http.StatusAccepted, gin.H{
		"request_id": result.RequestID,
		"status":     result.Status,
		"message":    "Request accepted for async processing",
	})
}

func (h *Handler) GetStatus(c *gin.Context) {
	requestID := c.Param("request_id")

	result, err := h.executor.GetStatus(requestID)
	if err != nil {
		h.logger.Warn("Status check failed",
			zap.String("request_id", requestID),
			zap.Error(err),
		)
		c.JSON(http.StatusNotFound, gin.H{
			"error": err.Error(),
		})
		return
	}

	resp := StatusResponse{
		RequestID:  result.RequestID,
		Status:     result.Status,
		CreatedAt:  result.CreatedAt,
		FinishedAt: result.FinishedAt,
	}

	if result.Stats != nil {
		resp.Stats = &ResourceStats{
			CPUTotal:     result.Stats.CPUTotal,
			CPUPercent:   result.Stats.CPUPercent,
			MemoryUsed:   result.Stats.MemoryUsed,
			MemoryMax:    result.Stats.MemoryMax,
			IOReadBytes:  result.Stats.IOReadBytes,
			IOWriteBytes: result.Stats.IOWriteBytes,
			IOReadRate:   result.Stats.IOReadRate,
			IOWriteRate:  result.Stats.IOWriteRate,
		}
	}

	c.JSON(http.StatusOK, resp)
}

func (h *Handler) Health(c *gin.Context) {
	c.JSON(http.StatusOK, HealthResponse{
		Status:    "healthy",
		Timestamp: time.Now().UTC().Format(time.RFC3339),
	})
}

func (h *Handler) ListPendingApprovals(c *gin.Context) {
	approvals := h.securityChecker.ListPendingApprovals()

	response := make([]PendingApprovalResponse, 0, len(approvals))
	for _, a := range approvals {
		response = append(response, PendingApprovalResponse{
			RequestID: a.RequestID,
			Command:   a.Command,
			Args:      a.Args,
			Score:     a.Score,
			Reasons:   a.Reasons,
			Category:  a.Category,
			Status:    string(a.Status),
			CreatedAt: a.CreatedAt.Format(time.RFC3339),
			ExpiresAt: a.ExpiresAt.Format(time.RFC3339),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"count":     len(response),
		"approvals": response,
	})
}

func (h *Handler) GetApproval(c *gin.Context) {
	approvalID := c.Param("approval_id")

	approval, ok := h.securityChecker.GetPendingApproval(approvalID)
	if !ok {
		c.JSON(http.StatusNotFound, gin.H{
			"error": "approval request not found: " + approvalID,
		})
		return
	}

	c.JSON(http.StatusOK, PendingApprovalResponse{
		RequestID: approval.RequestID,
		Command:   approval.Command,
		Args:      approval.Args,
		Score:     approval.Score,
		Reasons:   approval.Reasons,
		Category:  approval.Category,
		Status:    string(approval.Status),
		CreatedAt: approval.CreatedAt.Format(time.RFC3339),
		ExpiresAt: approval.ExpiresAt.Format(time.RFC3339),
	})
}

func (h *Handler) ResolveApproval(c *gin.Context) {
	approvalID := c.Param("approval_id")

	var req ApprovalRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": "invalid request: " + err.Error(),
		})
		return
	}

	if err := h.securityChecker.ResolveApproval(approvalID, req.Approved, req.Reason); err != nil {
		h.logger.Warn("Failed to resolve approval",
			zap.String("approval_id", approvalID),
			zap.Error(err),
		)
		c.JSON(http.StatusBadRequest, gin.H{
			"error": err.Error(),
		})
		return
	}

	action := "rejected"
	if req.Approved {
		action = "approved"
	}

	h.logger.Info("Approval resolved",
		zap.String("approval_id", approvalID),
		zap.String("action", action),
		zap.String("reason", req.Reason),
	)

	c.JSON(http.StatusOK, gin.H{
		"approval_id": approvalID,
		"action":      action,
		"status":      "resolved",
	})
}

func (h *Handler) convertToResponse(result *executor.ExecutionResponse) *ExecuteResponse {
	resp := &ExecuteResponse{
		RequestID:     result.RequestID,
		Status:        result.Status,
		ExitCode:      result.ExitCode,
		Stdout:        result.Stdout,
		Stderr:        result.Stderr,
		ExecutionTime: result.ExecutionTime.Seconds(),
		CreatedAt:     result.CreatedAt,
		FinishedAt:    result.FinishedAt,
		Error:         result.Error,
	}

	for _, f := range result.OutputFiles {
		resp.OutputFiles = append(resp.OutputFiles, OutputFile{
			Name: f.Name,
			Size: f.Size,
			Path: f.Path,
		})
	}

	if result.Stats != nil {
		resp.Stats = &ResourceStats{
			CPUTotal:     result.Stats.CPUTotal,
			CPUPercent:   result.Stats.CPUPercent,
			MemoryUsed:   result.Stats.MemoryUsed,
			MemoryMax:    result.Stats.MemoryMax,
			IOReadBytes:  result.Stats.IOReadBytes,
			IOWriteBytes: result.Stats.IOWriteBytes,
			IOReadRate:   result.Stats.IOReadRate,
			IOWriteRate:  result.Stats.IOWriteRate,
		}
	}

	return resp
}
