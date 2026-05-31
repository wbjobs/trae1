package api

import (
	"encoding/json"
	"io"
	"net/http"

	"github.com/gin-gonic/gin"

	"configvcs/pkg/astmerge"
	"configvcs/pkg/version"
)

type Handler struct {
	svc *version.Service
}

func NewHandler(svc *version.Service) *Handler {
	return &Handler{svc: svc}
}

func (h *Handler) SetupRoutes(r *gin.Engine) {
	api := r.Group("/api")
	{
		configs := api.Group("/configs")
		{
			configs.POST("", h.UploadConfig)
			configs.GET("/:cid", h.GetConfigByCID)
			configs.GET("/tag/:tag", h.GetConfigByTag)
			configs.GET("/commit/:id/content", h.GetCommitContent)
		}

		commits := api.Group("/commits")
		{
			commits.GET("/:id", h.GetCommit)
			commits.GET("/branch/:branch", h.ListCommits)
			commits.POST("", h.CreateCommit)
		}

		branches := api.Group("/branches")
		{
			branches.GET("", h.ListBranches)
			branches.GET("/:name", h.GetBranch)
			branches.POST("", h.CreateBranch)
			branches.DELETE("/:name", h.DeleteBranch)
		}

		tags := api.Group("/tags")
		{
			tags.GET("", h.ListTags)
			tags.GET("/:name", h.GetTag)
			tags.POST("", h.CreateTag)
			tags.DELETE("/:name", h.DeleteTag)
		}

		diff := api.Group("/diff")
		{
			diff.GET("/commits/:a/:b", h.DiffCommits)
			diff.GET("/cid/:cid", h.DiffCID)
			diff.GET("/ast/commits/:a/:b", h.ASTDiffCommits)
		}

		merge := api.Group("/merge")
		{
			merge.POST("", h.MergeBranches)
			merge.POST("/preview", h.PreviewMerge)
			merge.POST("/resolve", h.ResolveConflicts)
			merge.GET("/strategy/default", h.GetDefaultMergeStrategy)
		}

		api.GET("/tree/:branch", h.GetCommitTree)
	}
}

type UploadRequest struct {
	Branch  string `json:"branch" binding:"required"`
	Message string `json:"message"`
	Author  string `json:"author"`
	Format  string `json:"format"`
}

type UploadResponse struct {
	Commit *version.Commit `json:"commit"`
	CID    string          `json:"cid"`
}

func (h *Handler) UploadConfig(c *gin.Context) {
	branch := c.Query("branch")
	message := c.Query("message")
	author := c.Query("author")
	if branch == "" {
		branch = "main"
	}
	if message == "" {
		message = "upload config"
	}
	if author == "" {
		author = "anonymous"
	}

	file, _, err := c.Request.FormFile("file")
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "no file provided"})
		return
	}
	defer file.Close()

	content, err := io.ReadAll(file)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "read file failed"})
		return
	}

	commit, err := h.svc.Commit(branch, message, author, content)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, UploadResponse{
		Commit: commit,
		CID:    commit.CID,
	})
}

func (h *Handler) GetConfigByCID(c *gin.Context) {
	cid := c.Param("cid")
	content, err := h.svc.GetContentByCID(cid)
	if err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "config not found"})
		return
	}
	c.Data(http.StatusOK, "application/octet-stream", content)
}

func (h *Handler) GetConfigByTag(c *gin.Context) {
	tag := c.Param("tag")
	content, err := h.svc.GetContentByTag(tag)
	if err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": err.Error()})
		return
	}
	c.Data(http.StatusOK, "application/octet-stream", content)
}

func (h *Handler) GetCommitContent(c *gin.Context) {
	id := c.Param("id")
	content, err := h.svc.GetCommitContent(id)
	if err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "commit not found"})
		return
	}
	c.Data(http.StatusOK, "application/octet-stream", content)
}

func (h *Handler) GetCommit(c *gin.Context) {
	id := c.Param("id")
	commit, err := h.svc.GetCommit(id)
	if err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "commit not found"})
		return
	}
	c.JSON(http.StatusOK, commit)
}

func (h *Handler) ListCommits(c *gin.Context) {
	branch := c.Param("branch")
	commits, err := h.svc.ListCommits(branch)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, commits)
}

type CreateCommitRequest struct {
	Branch  string `json:"branch" binding:"required"`
	Message string `json:"message"`
	Author  string `json:"author"`
	Content string `json:"content"`
}

func (h *Handler) CreateCommit(c *gin.Context) {
	var req CreateCommitRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	if req.Message == "" {
		req.Message = "update config"
	}
	if req.Author == "" {
		req.Author = "anonymous"
	}

	commit, err := h.svc.Commit(req.Branch, req.Message, req.Author, []byte(req.Content))
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, commit)
}

func (h *Handler) ListBranches(c *gin.Context) {
	branches, err := h.svc.ListBranches()
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, branches)
}

func (h *Handler) GetBranch(c *gin.Context) {
	name := c.Param("name")
	branch, err := h.svc.GetBranch(name)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	if branch == nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "branch not found"})
		return
	}
	c.JSON(http.StatusOK, branch)
}

type CreateBranchRequest struct {
	Name string `json:"name" binding:"required"`
	From string `json:"from"`
}

func (h *Handler) CreateBranch(c *gin.Context) {
	var req CreateBranchRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	branch, err := h.svc.CreateBranch(req.Name, req.From)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, branch)
}

func (h *Handler) DeleteBranch(c *gin.Context) {
	name := c.Param("name")
	if err := h.svc.DeleteBranch(name); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"message": "branch deleted"})
}

func (h *Handler) ListTags(c *gin.Context) {
	tags, err := h.svc.ListTags()
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, tags)
}

func (h *Handler) GetTag(c *gin.Context) {
	name := c.Param("name")
	tag, err := h.svc.GetTag(name)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	if tag == nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "tag not found"})
		return
	}
	c.JSON(http.StatusOK, tag)
}

type CreateTagRequest struct {
	Name     string `json:"name" binding:"required"`
	CommitID string `json:"commit_id" binding:"required"`
	Message  string `json:"message"`
}

func (h *Handler) CreateTag(c *gin.Context) {
	var req CreateTagRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	tag, err := h.svc.CreateTag(req.Name, req.CommitID, req.Message)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, tag)
}

func (h *Handler) DeleteTag(c *gin.Context) {
	name := c.Param("name")
	if err := h.svc.DeleteTag(name); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"message": "tag deleted"})
}

func (h *Handler) DiffCommits(c *gin.Context) {
	a := c.Param("a")
	b := c.Param("b")

	diff, err := h.svc.Diff(a, b)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, diff)
}

func (h *Handler) DiffCID(c *gin.Context) {
	cid := c.Param("cid")

	diff, err := h.svc.DiffWithCID(cid)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, diff)
}

type MergeRequest struct {
	Source   string                  `json:"source" binding:"required"`
	Target   string                  `json:"target" binding:"required"`
	Author   string                  `json:"author"`
	Message  string                  `json:"message"`
	Strategy *astmerge.MergeStrategy `json:"strategy"`
}

func (h *Handler) MergeBranches(c *gin.Context) {
	var req MergeRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	if req.Author == "" {
		req.Author = "anonymous"
	}
	if req.Message == "" {
		req.Message = "Merge branch '" + req.Source + "' into '" + req.Target + "'"
	}

	result, err := h.svc.Merge(req.Source, req.Target, req.Author, req.Message, req.Strategy)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, result)
}

type PreviewMergeRequest struct {
	Base     string                  `json:"base" binding:"required"`
	Source   string                  `json:"source" binding:"required"`
	Target   string                  `json:"target" binding:"required"`
	Strategy *astmerge.MergeStrategy `json:"strategy"`
}

func (h *Handler) PreviewMerge(c *gin.Context) {
	var req PreviewMergeRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	result, err := h.svc.PreviewMerge(req.Base, req.Source, req.Target, req.Strategy)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, result)
}

type ResolveConflictsRequest struct {
	MergeResult  *astmerge.MergeResult `json:"merge_result" binding:"required"`
	Resolutions   map[string]string        `json:"resolutions" binding:"required"`
	TargetBranch  string                    `json:"target_branch" binding:"required"`
	Message       string                    `json:"message"`
	Author        string                    `json:"author"`
}

func (h *Handler) ResolveConflicts(c *gin.Context) {
	var req ResolveConflictsRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	if req.Author == "" {
		req.Author = "anonymous"
	}
	if req.Message == "" {
		req.Message = "Resolve merge conflicts"
	}

	svcReq := &version.ResolveConflictRequest{
		MergeResult:  req.MergeResult,
		Resolutions:  req.Resolutions,
		TargetBranch: req.TargetBranch,
		Message:      req.Message,
		Author:       req.Author,
	}

	result, err := h.svc.ResolveAndCommit(svcReq)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	if !result.Success {
		c.JSON(http.StatusBadRequest, result)
		return
	}

	c.JSON(http.StatusOK, result)
}

func (h *Handler) GetDefaultMergeStrategy(c *gin.Context) {
	strategy := h.svc.GetDefaultMergeStrategy()
	c.JSON(http.StatusOK, strategy)
}

func (h *Handler) ASTDiffCommits(c *gin.Context) {
	a := c.Param("a")
	b := c.Param("b")

	diff, err := h.svc.ASTDiff(a, b)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, diff)
}

func (h *Handler) GetCommitTree(c *gin.Context) {
	branch := c.Param("branch")
	commits, err := h.svc.GetCommitTree(branch)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, commits)
}

func RespondJSON(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}
