package handler

import (
	"log"
	"net/http"
	"time"

	"api-signature/model"
	"api-signature/service"
	"api-signature/util"

	"github.com/gin-gonic/gin"
)

type ApiHandler struct {
	signatureService     *service.SignatureService
	clientService        *service.ClientService
	permissionService    *service.PermissionService
	blacklistService     *service.BlacklistService
	auditService         *service.AuditService
	secretRotationService *service.SecretRotationService
	statsService         *service.StatsService
}

func NewApiHandler(
	signatureService *service.SignatureService,
	clientService *service.ClientService,
	permissionService *service.PermissionService,
	blacklistService *service.BlacklistService,
	auditService *service.AuditService,
	secretRotationService *service.SecretRotationService,
	statsService *service.StatsService,
) *ApiHandler {
	return &ApiHandler{
		signatureService:     signatureService,
		clientService:        clientService,
		permissionService:    permissionService,
		blacklistService:     blacklistService,
		auditService:         auditService,
		secretRotationService: secretRotationService,
		statsService:         statsService,
	}
}

type GenerateSignatureRequest struct {
	ClientID string            `json:"client_id" binding:"required"`
	Method   string            `json:"method" binding:"required"`
	Path     string            `json:"path" binding:"required"`
	Body     string            `json:"body"`
	Params   map[string]string `json:"params"`
}

type GenerateSignatureResponse struct {
	Signature string `json:"signature"`
	Timestamp int64  `json:"timestamp"`
	Nonce     string `json:"nonce"`
}

func (h *ApiHandler) GenerateSignature(c *gin.Context) {
	var req GenerateSignatureRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	client, err := h.clientService.GetClient(req.ClientID)
	if err != nil {
		util.UnauthorizedResponse(c, model.ErrCodeClientNotFound)
		return
	}

	timestamp := util.GetCurrentTimestamp()
	nonce := util.GenerateNonce()

	signature := util.GenerateSignature(
		client.ClientSecret,
		req.Params,
		req.Method,
		req.Path,
		req.Body,
		timestamp,
		nonce,
	)

	util.SuccessResponse(c, GenerateSignatureResponse{
		Signature: signature,
		Timestamp: timestamp,
		Nonce:     nonce,
	})
}

func (h *ApiHandler) HealthCheck(c *gin.Context) {
	util.SuccessResponse(c, gin.H{
		"status": "healthy",
		"time":   time.Now().Format("2006-01-02 15:04:05"),
	})
}

type VerifyRequestRequest struct {
	ClientID  string `json:"client_id" binding:"required"`
	Signature string `json:"signature" binding:"required"`
	Timestamp int64  `json:"timestamp" binding:"required"`
	Nonce     string `json:"nonce" binding:"required"`
	Method    string `json:"method" binding:"required"`
	Path      string `json:"path" binding:"required"`
	Body      string `json:"body"`
}

func (h *ApiHandler) VerifyRequest(c *gin.Context) {
	var req VerifyRequestRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("VerifyRequest bind error: %v", err)
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	if err := h.signatureService.ValidateTimestamp(req.Timestamp); err != nil {
		log.Printf("VerifyRequest timestamp error: %v", err)
		util.BadRequestResponse(c, model.ErrCodeTimestampExpired)
		return
	}

	if _, err := h.clientService.GetClient(req.ClientID); err != nil {
		log.Printf("VerifyRequest client error: %v", err)
		util.UnauthorizedResponse(c, model.ErrCodeClientNotFound)
		return
	}

	params := make(map[string]string)
	if err := h.signatureService.VerifySignature(
		req.ClientID,
		req.Signature,
		params,
		req.Method,
		req.Path,
		req.Body,
		req.Timestamp,
		req.Nonce,
	); err != nil {
		log.Printf("VerifyRequest signature error: client=%s, method=%s, path=%s, error=%v",
			req.ClientID, req.Method, req.Path, err)
		util.UnauthorizedResponse(c, model.ErrCodeInvalidSignature)
		return
	}

	util.SuccessResponse(c, gin.H{
		"verified": true,
		"message":  "Signature verification successful",
	})
}

func (h *ApiHandler) GetClientInfo(c *gin.Context) {
	clientID := c.Param("client_id")
	if clientID == "" {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	client, err := h.clientService.GetClient(clientID)
	if err != nil {
		util.UnauthorizedResponse(c, model.ErrCodeClientNotFound)
		return
	}

	util.SuccessResponse(c, gin.H{
		"client_id":    client.ClientID,
		"permissions":  client.Permissions,
		"rate_limit":   client.RateLimit,
		"enabled":      client.Enabled,
	})
}

func (h *ApiHandler) GetAllClients(c *gin.Context) {
	clients := h.clientService.GetAllClients()

	var clientInfos []gin.H
	for _, client := range clients {
		clientInfos = append(clientInfos, gin.H{
			"client_id":   client.ClientID,
			"permissions": client.Permissions,
			"rate_limit":  client.RateLimit,
			"enabled":     client.Enabled,
		})
	}

	util.SuccessResponse(c, clientInfos)
}

type AddBlacklistRequest struct {
	IP       string `json:"ip" binding:"required"`
	Reason   string `json:"reason"`
	Duration int    `json:"duration"`
}

func (h *ApiHandler) AddBlacklist(c *gin.Context) {
	var req AddBlacklistRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	duration := time.Duration(req.Duration) * time.Second
	if duration <= 0 {
		duration = 24 * time.Hour
	}

	if err := h.blacklistService.AddIP(req.IP, req.Reason, duration); err != nil {
		util.InternalServerErrorResponse(c)
		return
	}

	util.SuccessResponse(c, gin.H{
		"message": "IP added to blacklist successfully",
		"ip":      req.IP,
	})
}

func (h *ApiHandler) RemoveBlacklist(c *gin.Context) {
	ip := c.Param("ip")
	if ip == "" {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	if err := h.blacklistService.RemoveIP(ip); err != nil {
		util.InternalServerErrorResponse(c)
		return
	}

	util.SuccessResponse(c, gin.H{
		"message": "IP removed from blacklist successfully",
		"ip":      ip,
	})
}

func (h *ApiHandler) CheckIPStatus(c *gin.Context) {
	ip := c.Query("ip")
	if ip == "" {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	blocked, reason, err := h.blacklistService.IsIPBlocked(ip)
	if err != nil {
		util.InternalServerErrorResponse(c)
		return
	}

	whitelisted, err := h.blacklistService.IsIPWhitelisted(ip)
	if err != nil {
		util.InternalServerErrorResponse(c)
		return
	}

	util.SuccessResponse(c, gin.H{
		"ip":          ip,
		"is_blocked":  blocked,
		"block_reason": reason,
		"is_whitelisted": whitelisted,
	})
}

type ProtectedDataRequest struct {
	Data string `json:"data"`
}

func (h *ApiHandler) ProtectedRead(c *gin.Context) {
	clientID, _ := c.Get("client_id")

	util.SuccessResponse(c, gin.H{
		"message":   "Read access granted",
		"client_id": clientID,
		"data":      "This is protected read data",
	})
}

func (h *ApiHandler) ProtectedWrite(c *gin.Context) {
	clientID, _ := c.Get("client_id")

	var req ProtectedDataRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	util.SuccessResponse(c, gin.H{
		"message":   "Write access granted",
		"client_id": clientID,
		"received":  req.Data,
	})
}

func (h *ApiHandler) ProtectedAdmin(c *gin.Context) {
	clientID, _ := c.Get("client_id")

	clients := h.clientService.GetAllClients()
	var clientList []gin.H
	for _, client := range clients {
		clientList = append(clientList, gin.H{
			"client_id":   client.ClientID,
			"permissions": client.Permissions,
			"enabled":     client.Enabled,
		})
	}

	util.SuccessResponse(c, gin.H{
		"message":   "Admin access granted",
		"client_id": clientID,
		"clients":   clientList,
	})
}

func (h *ApiHandler) GetRateLimit(c *gin.Context) {
	clientID := c.Param("client_id")
	if clientID == "" {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	count, err := h.clientService.GetRateLimitCount(clientID)
	if err != nil {
		util.InternalServerErrorResponse(c)
		return
	}

	client, _ := h.clientService.GetClient(clientID)
	rateLimit := 0
	if client != nil {
		rateLimit = client.RateLimit
	}

	util.SuccessResponse(c, gin.H{
		"client_id":   clientID,
		"current":     count,
		"rate_limit":  rateLimit,
		"remaining":   rateLimit - int(count),
	})
}

func (h *ApiHandler) GetPermissions(c *gin.Context) {
	permissions := h.permissionService.GetAvailablePermissions()
	util.SuccessResponse(c, gin.H{
		"permissions": permissions,
	})
}

func (h *ApiHandler) RotateSecret(c *gin.Context) {
	var req model.SecretRotationRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	gracePeriod := req.GracePeriod
	if gracePeriod <= 0 {
		gracePeriod = 24
	}

	newVersion, err := h.secretRotationService.RotateSecret(req.ClientID, req.NewSecret, gracePeriod)
	if err != nil {
		log.Printf("Failed to rotate secret for client %s: %v", req.ClientID, err)
		util.InternalServerErrorResponse(c)
		return
	}

	util.SuccessResponse(c, gin.H{
		"message":      "Secret rotated successfully",
		"client_id":    req.ClientID,
		"new_version":  newVersion.Version,
		"new_secret":   newVersion.Secret,
		"expires_at":   newVersion.ExpiresAt,
		"grace_period": gracePeriod,
	})
}

func (h *ApiHandler) GetSecretHistory(c *gin.Context) {
	clientID := c.Param("client_id")
	if clientID == "" {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	history, err := h.secretRotationService.GetSecretHistory(clientID)
	if err != nil {
		util.InternalServerErrorResponse(c)
		return
	}

	util.SuccessResponse(c, gin.H{
		"client_id": clientID,
		"history":   history,
	})
}

func (h *ApiHandler) GetAbnormalStats(c *gin.Context) {
	stats, err := h.statsService.GetAbnormalStats()
	if err != nil {
		util.InternalServerErrorResponse(c)
		return
	}

	util.SuccessResponse(c, stats)
}

func (h *ApiHandler) GetErrorStats(c *gin.Context) {
	errorType := c.Query("type")
	
	if errorType != "" {
		stats, err := h.statsService.GetErrorStats(errorType)
		if err != nil {
			util.InternalServerErrorResponse(c)
			return
		}
		util.SuccessResponse(c, gin.H{
			"error_type": errorType,
			"stats":      stats,
		})
		return
	}

	stats, err := h.statsService.GetErrorStats("")
	if err != nil {
		util.InternalServerErrorResponse(c)
		return
	}

	util.SuccessResponse(c, stats)
}

func (h *ApiHandler) GetAbnormalIPs(c *gin.Context) {
	ips := h.statsService.GetAbnormalIPs()
	util.SuccessResponse(c, gin.H{
		"abnormal_ips": ips,
	})
}

func (h *ApiHandler) GetSecurityAlerts(c *gin.Context) {
	alerts, err := h.statsService.GetSecurityAlerts(100)
	if err != nil {
		util.InternalServerErrorResponse(c)
		return
	}

	util.SuccessResponse(c, gin.H{
		"alerts": alerts,
		"count":  len(alerts),
	})
}

type SetEndpointLimitRequest struct {
	ClientID string `json:"client_id" binding:"required"`
	Path     string `json:"path" binding:"required"`
	Limit    int    `json:"limit" binding:"required"`
}

func (h *ApiHandler) SetEndpointLimit(c *gin.Context) {
	var req SetEndpointLimitRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	if req.Limit <= 0 {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	if err := h.clientService.SetEndpointLimit(req.ClientID, req.Path, req.Limit); err != nil {
		log.Printf("Failed to set endpoint limit: %v", err)
		util.InternalServerErrorResponse(c)
		return
	}

	util.SuccessResponse(c, gin.H{
		"message":   "Endpoint limit set successfully",
		"client_id": req.ClientID,
		"path":      req.Path,
		"limit":     req.Limit,
	})
}

func (h *ApiHandler) GetRateLimitInfo(c *gin.Context) {
	clientID := c.Param("client_id")
	if clientID == "" {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	info, err := h.clientService.GetRateLimitInfo(clientID)
	if err != nil {
		util.InternalServerErrorResponse(c)
		return
	}

	util.SuccessResponse(c, info)
}

type CreateAlertRequest struct {
	Type        string `json:"type" binding:"required"`
	Severity    string `json:"severity" binding:"required"`
	ClientID    string `json:"client_id"`
	IP          string `json:"ip"`
	Description string `json:"description" binding:"required"`
}

func (h *ApiHandler) CreateSecurityAlert(c *gin.Context) {
	var req CreateAlertRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
		return
	}

	alert := h.statsService.CreateSecurityAlert(req.Type, req.Severity, req.ClientID, req.IP, req.Description)

	util.SuccessResponse(c, gin.H{
		"message": "Alert created successfully",
		"alert":   alert,
	})
}

func (h *ApiHandler) NotFound(c *gin.Context) {
	c.JSON(http.StatusNotFound, model.ApiResponse{
		Code:    404,
		Message: "API endpoint not found",
		Time:    time.Now().Unix(),
	})
}
