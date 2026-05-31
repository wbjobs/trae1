package model

import "time"

type ApiRequest struct {
	ClientID   string            `json:"client_id" form:"client_id" binding:"required"`
	Timestamp  int64             `json:"timestamp" form:"timestamp" binding:"required"`
	Nonce      string            `json:"nonce" form:"nonce" binding:"required"`
	Signature  string            `json:"signature" form:"signature" binding:"required"`
	Method     string            `json:"method" form:"method"`
	Path       string            `json:"path" form:"path"`
	Body       string            `json:"body" form:"body"`
	Params     map[string]string `json:"params" form:"params"`
}

type SignatureData struct {
	ClientID   string `json:"client_id"`
	Timestamp  int64  `json:"timestamp"`
	Nonce      string `json:"nonce"`
	Method     string `json:"method"`
	Path       string `json:"path"`
	BodyHash   string `json:"body_hash"`
	ParamsHash string `json:"params_hash"`
}

type ClientInfo struct {
	ClientID        string            `json:"client_id"`
	ClientSecret    string            `json:"client_secret"`
	SecretVersion   int               `json:"secret_version"`
	SecretHistory   []SecretVersion   `json:"secret_history,omitempty"`
	Permissions     []string          `json:"permissions"`
	RateLimit       int               `json:"rate_limit"`
	EndpointLimits  map[string]int    `json:"endpoint_limits,omitempty"`
	Enabled         bool              `json:"enabled"`
}

type SecretVersion struct {
	Version   int       `json:"version"`
	Secret    string    `json:"secret"`
	CreatedAt time.Time `json:"created_at"`
	ExpiresAt time.Time `json:"expires_at"`
	IsActive  bool      `json:"is_active"`
}

type BlacklistEntry struct {
	IP        string `json:"ip"`
	Reason    string `json:"reason"`
	CreatedAt int64  `json:"created_at"`
	ExpiresAt int64  `json:"expires_at"`
}

type AuditLog struct {
	ID          string `json:"id"`
	ClientID    string `json:"client_id"`
	IP          string `json:"ip"`
	Method      string `json:"method"`
	Path        string `json:"path"`
	StatusCode  int    `json:"status_code"`
	Timestamp   int64  `json:"timestamp"`
	Signature   string `json:"signature"`
	Nonce       string `json:"nonce"`
	RequestTime int64  `json:"request_time"`
	ErrorMsg    string `json:"error_msg,omitempty"`
	ErrorType   string `json:"error_type,omitempty"`
}

type PermissionLevel int

const (
	PermissionNone  PermissionLevel = iota
	PermissionRead
	PermissionWrite
	PermissionAdmin
)

func (p PermissionLevel) String() string {
	switch p {
	case PermissionRead:
		return "api:read"
	case PermissionWrite:
		return "api:write"
	case PermissionAdmin:
		return "api:admin"
	default:
		return "none"
	}
}

type ApiResponse struct {
	Code    int         `json:"code"`
	Message string      `json:"message"`
	Data    interface{} `json:"data,omitempty"`
	Time    int64       `json:"time"`
}

type ErrorCode int

const (
	ErrCodeSuccess           ErrorCode = 0
	ErrCodeInvalidRequest    ErrorCode = 1001
	ErrCodeInvalidSignature  ErrorCode = 1002
	ErrCodeTimestampExpired  ErrorCode = 1003
	ErrCodeNonceReplay       ErrorCode = 1004
	ErrCodeIPBlocked         ErrorCode = 1005
	ErrCodeClientNotFound    ErrorCode = 1006
	ErrCodePermissionDenied  ErrorCode = 1007
	ErrCodeRateLimitExceeded ErrorCode = 1008
	ErrCodeInternalError     ErrorCode = 2001
	ErrCodeTamperingDetected ErrorCode = 1009
	ErrCodeSecretExpired     ErrorCode = 1010
	ErrCodeAbnormalRequest   ErrorCode = 1011
)

func (e ErrorCode) String() string {
	switch e {
	case ErrCodeSuccess:
		return "success"
	case ErrCodeInvalidRequest:
		return "invalid request parameters"
	case ErrCodeInvalidSignature:
		return "invalid signature"
	case ErrCodeTimestampExpired:
		return "timestamp expired or out of tolerance"
	case ErrCodeNonceReplay:
		return "nonce replay attack detected"
	case ErrCodeIPBlocked:
		return "ip address is blocked"
	case ErrCodeClientNotFound:
		return "client not found or disabled"
	case ErrCodePermissionDenied:
		return "permission denied"
	case ErrCodeRateLimitExceeded:
		return "rate limit exceeded"
	case ErrCodeInternalError:
		return "internal server error"
	case ErrCodeTamperingDetected:
		return "parameter tampering detected"
	case ErrCodeSecretExpired:
		return "client secret has expired, please rotate"
	case ErrCodeAbnormalRequest:
		return "abnormal request pattern detected"
	default:
		return "unknown error"
	}
}

type AbnormalStats struct {
	TotalRequests      int64            `json:"total_requests"`
	ValidRequests      int64            `json:"valid_requests"`
	InvalidRequests    int64            `json:"invalid_requests"`
	SignatureErrors    int64            `json:"signature_errors"`
	TimestampErrors    int64            `json:"timestamp_errors"`
	NonceReplayAttacks int64            `json:"nonce_replay_attacks"`
	IPBlockedRequests  int64            `json:"ip_blocked_requests"`
	PermissionDenied   int64            `json:"permission_denied"`
	RateLimitExceeded  int64            `json:"rate_limit_exceeded"`
	TamperingDetected  int64            `json:"tampering_detected"`
	ClientStats        map[string]int64 `json:"client_stats,omitempty"`
	EndpointStats      map[string]int64 `json:"endpoint_stats,omitempty"`
	IPStats            map[string]int64 `json:"ip_stats,omitempty"`
	ErrorTypeStats     map[string]int64 `json:"error_type_stats,omitempty"`
	LastUpdated        int64            `json:"last_updated"`
}

type RateLimitInfo struct {
	ClientID     string         `json:"client_id"`
	GlobalLimit  int            `json:"global_limit"`
	GlobalCount  int64          `json:"global_count"`
	GlobalReset  int64          `json:"global_reset"`
	EndpointLimits map[string]EndpointLimit `json:"endpoint_limits,omitempty"`
}

type EndpointLimit struct {
	Path       string `json:"path"`
	Limit      int    `json:"limit"`
	Count      int64  `json:"count"`
	ResetTime  int64  `json:"reset_time"`
}

type SecretRotationRequest struct {
	ClientID    string `json:"client_id" binding:"required"`
	NewSecret   string `json:"new_secret"`
	GracePeriod int    `json:"grace_period"`
}

type SecurityAlert struct {
	ID          string    `json:"id"`
	Type        string    `json:"type"`
	Severity    string    `json:"severity"`
	ClientID    string    `json:"client_id,omitempty"`
	IP          string    `json:"ip,omitempty"`
	Description string    `json:"description"`
	Timestamp   int64     `json:"timestamp"`
	Resolved    bool      `json:"resolved"`
}
