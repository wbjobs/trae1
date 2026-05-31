package middleware

import (
	"log"

	"api-signature/model"
	"api-signature/service"
	"api-signature/util"

	"github.com/gin-gonic/gin"
)

func TimestampMiddleware(signatureService *service.SignatureService) gin.HandlerFunc {
	return func(c *gin.Context) {
		timestamp, exists := c.Get("timestamp")
		if !exists {
			util.BadRequestResponse(c, model.ErrCodeTimestampExpired)
			c.Abort()
			return
		}

		ts, ok := timestamp.(int64)
		if !ok {
			util.BadRequestResponse(c, model.ErrCodeTimestampExpired)
			c.Abort()
			return
		}

		if err := signatureService.ValidateTimestamp(ts); err != nil {
			log.Printf("Timestamp validation failed: %v", err)
			util.BadRequestResponse(c, model.ErrCodeTimestampExpired)
			c.Abort()
			return
		}

		c.Next()
	}
}

func NonceMiddleware(signatureService *service.SignatureService) gin.HandlerFunc {
	return func(c *gin.Context) {
		nonce, exists := c.Get("nonce")
		if !exists {
			util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
			c.Abort()
			return
		}

		nonceStr, ok := nonce.(string)
		if !ok || nonceStr == "" {
			util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
			c.Abort()
			return
		}

		clientID, _ := c.Get("client_id")
		clientIDStr, _ := clientID.(string)

		if err := signatureService.CheckNonceReplay(nonceStr, clientIDStr); err != nil {
			log.Printf("Nonce replay attack detected: %v, nonce: %s, client: %s", err, nonceStr, clientIDStr)
			util.BadRequestResponse(c, model.ErrCodeNonceReplay)
			c.Abort()
			return
		}

		c.Next()
	}
}
