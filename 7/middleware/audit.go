package middleware

import (
	"time"

	"api-signature/service"

	"github.com/gin-gonic/gin"
)

func AuditMiddleware(auditService *service.AuditService) gin.HandlerFunc {
	return func(c *gin.Context) {
		startTime := time.Now()

		c.Next()

		clientID, _ := c.Get("client_id")
		clientIDStr, _ := clientID.(string)

		clientIP, _ := c.Get("client_ip")
		clientIPStr, _ := clientIP.(string)

		signature, _ := c.Get("signature")
		signatureStr, _ := signature.(string)

		nonce, _ := c.Get("nonce")
		nonceStr, _ := nonce.(string)

		statusCode := c.Writer.Status()

		var errMsg string
		if len(c.Errors) > 0 {
			errMsg = c.Errors.Last().Error()
		}

		auditService.LogRequest(
			clientIDStr,
			clientIPStr,
			c.Request.Method,
			c.Request.URL.Path,
			statusCode,
			signatureStr,
			nonceStr,
			startTime,
			errMsg,
		)
	}
}
