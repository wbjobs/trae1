package middleware

import (
	"log"

	"api-signature/model"
	"api-signature/service"
	"api-signature/util"

	"github.com/gin-gonic/gin"
)

func PermissionMiddleware(permissionService *service.PermissionService, clientService *service.ClientService, requiredPermission string) gin.HandlerFunc {
	return func(c *gin.Context) {
		if requiredPermission == "" {
			c.Next()
			return
		}

		clientID, exists := c.Get("client_id")
		if !exists {
			log.Printf("Permission check failed: client_id not found in context")
			util.UnauthorizedResponse(c, model.ErrCodePermissionDenied)
			c.Abort()
			return
		}

		clientIDStr, ok := clientID.(string)
		if !ok || clientIDStr == "" {
			log.Printf("Permission check failed: invalid client_id type")
			util.UnauthorizedResponse(c, model.ErrCodePermissionDenied)
			c.Abort()
			return
		}

		client, err := clientService.GetClient(clientIDStr)
		if err != nil {
			log.Printf("Failed to get client for permission check: %v", err)
			util.UnauthorizedResponse(c, model.ErrCodeClientNotFound)
			c.Abort()
			return
		}

		if err := permissionService.CheckPermission(client, requiredPermission); err != nil {
			log.Printf("Permission check failed for client %s: %v, required: %s, has: %v",
				clientIDStr, err, requiredPermission, client.Permissions)
			
			statsService := service.NewStatsService()
			statsService.RecordError("permission_denied", clientIDStr, "")
			
			util.ForbiddenResponse(c, model.ErrCodePermissionDenied)
			c.Abort()
			return
		}

		c.Next()
	}
}

func RateLimitMiddleware(clientService *service.ClientService) gin.HandlerFunc {
	return func(c *gin.Context) {
		clientID, exists := c.Get("client_id")
		if !exists {
			c.Next()
			return
		}

		clientIDStr, ok := clientID.(string)
		if !ok {
			c.Next()
			return
		}

		allowed, count, err := clientService.CheckRateLimit(clientIDStr)
		if err != nil {
			log.Printf("Rate limit check error: %v", err)
			c.Next()
			return
		}

		if !allowed {
			log.Printf("Rate limit exceeded for client %s, current count: %d", clientIDStr, count)
			
			statsService := service.NewStatsService()
			statsService.RecordError("rate_limit", clientIDStr, "")
			
			util.TooManyRequestsResponse(c, model.ErrCodeRateLimitExceeded)
			c.Abort()
			return
		}

		c.Next()
	}
}

func EndpointRateLimitMiddleware(clientService *service.ClientService) gin.HandlerFunc {
	return func(c *gin.Context) {
		clientID, exists := c.Get("client_id")
		if !exists {
			c.Next()
			return
		}

		clientIDStr, ok := clientID.(string)
		if !ok {
			c.Next()
			return
		}

		path := c.Request.URL.Path
		allowed, count, err := clientService.CheckEndpointRateLimit(clientIDStr, path)
		if err != nil {
			log.Printf("Endpoint rate limit check error: %v", err)
			c.Next()
			return
		}

		if !allowed {
			log.Printf("Endpoint rate limit exceeded for client %s on path %s, current count: %d", clientIDStr, path, count)
			util.TooManyRequestsResponse(c, model.ErrCodeRateLimitExceeded)
			c.Abort()
			return
		}

		c.Next()
	}
}
