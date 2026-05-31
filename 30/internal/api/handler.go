package api

import (
	"net/http"

	"github.com/gin-gonic/gin"

	"transcode-gateway/internal/model"
	"transcode-gateway/internal/scheduler"
)

type Handler struct {
	s *scheduler.Scheduler
}

func NewHandler(s *scheduler.Scheduler) *Handler {
	return &Handler{s: s}
}

func (h *Handler) Register(r *gin.Engine) {
	api := r.Group("/api/v1")
	{
		api.POST("/tasks", h.createTask)
		api.GET("/tasks", h.listTasks)
		api.GET("/tasks/queue", h.queueStatus)
		api.GET("/tasks/:id", h.getTask)
		api.DELETE("/tasks/:id", h.stopTask)
	}
}

func (h *Handler) createTask(c *gin.Context) {
	var req model.TaskRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	task, err := h.s.Start(req)
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusCreated, task)
}

func (h *Handler) listTasks(c *gin.Context) {
	c.JSON(http.StatusOK, h.s.List())
}

func (h *Handler) queueStatus(c *gin.Context) {
	snap := h.s.QueueSnapshot()
	c.JSON(http.StatusOK, snap)
}

func (h *Handler) getTask(c *gin.Context) {
	id := c.Param("id")
	t, ok := h.s.Get(id)
	if !ok {
		c.JSON(http.StatusNotFound, gin.H{"error": "任务不存在"})
		return
	}
	c.JSON(http.StatusOK, t)
}

func (h *Handler) stopTask(c *gin.Context) {
	id := c.Param("id")
	if err := h.s.Stop(id); err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"status": "stopped", "id": id})
}
