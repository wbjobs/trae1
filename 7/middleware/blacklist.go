package middleware

import (
	"log"
	"net"

	"api-signature/model"
	"api-signature/service"
	"api-signature/util"

	"github.com/gin-gonic/gin"
)

func BlacklistMiddleware(blacklistService *service.BlacklistService) gin.HandlerFunc {
	return func(c *gin.Context) {
		clientIP := getClientIP(c)

		if err := blacklistService.CheckIPAccess(clientIP); err != nil {
			log.Printf("IP blocked: %s, reason: %v", clientIP, err)
			util.ForbiddenResponse(c, model.ErrCodeIPBlocked)
			c.Abort()
			return
		}

		c.Set("client_ip", clientIP)
		c.Next()
	}
}

func getClientIP(c *gin.Context) string {
	ip := c.GetHeader("X-Forwarded-For")
	if ip != "" {
		ips := splitAndTrim(ip)
		if len(ips) > 0 {
			return ips[0]
		}
	}

	ip = c.GetHeader("X-Real-IP")
	if ip != "" {
		return ip
	}

	ip, _, err := net.SplitHostPort(c.Request.RemoteAddr)
	if err != nil {
		return c.Request.RemoteAddr
	}

	return ip
}

func splitAndTrim(s string) []string {
	var result []string
	for _, part := range splitString(s, ",") {
		part = trimSpace(part)
		if part != "" {
			result = append(result, part)
		}
	}
	return result
}

func splitString(s, sep string) []string {
	var result []string
	start := 0
	for i := 0; i < len(s); i++ {
		if s[i:i+1] == sep {
			result = append(result, s[start:i])
			start = i + 1
		}
	}
	result = append(result, s[start:])
	return result
}

func trimSpace(s string) string {
	start := 0
	end := len(s)
	for start < end && (s[start] == ' ' || s[start] == '\t') {
		start++
	}
	for end > start && (s[end-1] == ' ' || s[end-1] == '\t') {
		end--
	}
	return s[start:end]
}
