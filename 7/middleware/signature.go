package middleware

import (
	"bytes"
	"encoding/json"
	"io"
	"log"
	"strings"

	"api-signature/model"
	"api-signature/service"
	"api-signature/util"

	"github.com/gin-gonic/gin"
)

func SignatureMiddleware(signatureService *service.SignatureService, clientService *service.ClientService) gin.HandlerFunc {
	return func(c *gin.Context) {
		var req model.ApiRequest
		var rawBody string

		contentType := c.ContentType()

		if strings.Contains(contentType, "application/json") {
			bodyBytes, err := io.ReadAll(c.Request.Body)
			if err != nil {
				log.Printf("Failed to read request body: %v", err)
				util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
				c.Abort()
				return
			}
			c.Request.Body = io.NopCloser(bytes.NewBuffer(bodyBytes))
			rawBody = string(bodyBytes)

			if err := json.Unmarshal(bodyBytes, &req); err != nil {
				log.Printf("Failed to parse JSON body: %v", err)
				util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
				c.Abort()
				return
			}
		} else {
			if err := c.ShouldBind(&req); err != nil {
				log.Printf("Failed to bind form: %v", err)
				util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
				c.Abort()
				return
			}
		}

		if req.ClientID == "" {
			req.ClientID = c.Query("client_id")
		}
		if req.Signature == "" {
			req.Signature = c.Query("signature")
		}
		if req.Nonce == "" {
			req.Nonce = c.Query("nonce")
		}
		if req.Timestamp == 0 {
			if ts := c.Query("timestamp"); ts != "" {
				if t, err := parseInt64(ts); err == nil {
					req.Timestamp = t
				}
			}
		}

		if req.ClientID == "" || req.Signature == "" || req.Nonce == "" || req.Timestamp == 0 {
			log.Printf("Missing required signature parameters: client_id=%s, has_signature=%v, nonce=%s, timestamp=%d",
				req.ClientID, req.Signature != "", req.Nonce, req.Timestamp)
			util.BadRequestResponse(c, model.ErrCodeInvalidRequest)
			c.Abort()
			return
		}

		_, err := clientService.GetClient(req.ClientID)
		if err != nil {
			log.Printf("Client not found: %s, error: %v", req.ClientID, err)
			util.UnauthorizedResponse(c, model.ErrCodeClientNotFound)
			c.Abort()
			return
		}

		method := c.Request.Method
		path := c.Request.URL.Path

		params := make(map[string]string)
		for key, values := range c.Request.URL.Query() {
			if key != "client_id" && key != "timestamp" && key != "nonce" && key != "signature" {
				params[key] = strings.Join(values, ",")
			}
		}

		bodyForSign := rawBody
		if bodyForSign == "" {
			bodyForSign = c.PostForm("body")
		}

		if err := signatureService.VerifySignature(
			req.ClientID,
			req.Signature,
			params,
			method,
			path,
			bodyForSign,
			req.Timestamp,
			req.Nonce,
		); err != nil {
			log.Printf("Signature verification failed: client=%s, method=%s, path=%s, error=%v",
				req.ClientID, method, path, err)
			util.UnauthorizedResponse(c, model.ErrCodeInvalidSignature)
			c.Abort()
			return
		}

		c.Set("client_id", req.ClientID)
		c.Set("signature", req.Signature)
		c.Set("nonce", req.Nonce)
		c.Set("timestamp", req.Timestamp)
		c.Set("request_body", rawBody)

		c.Next()
	}
}

func parseInt64(s string) (int64, error) {
	var n int64
	for _, c := range s {
		if c < '0' || c > '9' {
			return 0, nil
		}
		n = n*10 + int64(c-'0')
	}
	return n, nil
}
