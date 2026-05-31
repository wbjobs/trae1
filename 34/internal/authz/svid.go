package authz

import (
	"context"
	"fmt"
	"strings"

	"github.com/golang-jwt/jwt/v5"
	"msgbridge/internal/config"
	"msgbridge/internal/logger"
)

type SVIDVerifier struct {
	audience string
	issuer   string
	jwksURL  string
}

func NewSVIDVerifier(cfg config.SVIDConfig) *SVIDVerifier {
	return &SVIDVerifier{
		audience: cfg.Audience,
		issuer:   cfg.Issuer,
		jwksURL:  cfg.JWKSURL,
	}
}

func (v *SVIDVerifier) Verify(ctx context.Context, tokenStr string) (*SVIDClaims, error) {
	if tokenStr == "" {
		return nil, fmt.Errorf("empty token")
	}

	if strings.HasPrefix(tokenStr, "Bearer ") {
		tokenStr = tokenStr[7:]
	}

	token, err := jwt.Parse(tokenStr, func(token *jwt.Token) (interface{}, error) {
		if _, ok := token.Method.(*jwt.SigningMethodRSA); !ok {
			return nil, fmt.Errorf("unexpected signing method: %v", token.Header["alg"])
		}
		return nil, fmt.Errorf("jwks verification not implemented inline")
	})

	if err != nil {
		return nil, fmt.Errorf("parse token: %w", err)
	}

	claims, ok := token.Claims.(jwt.MapClaims)
	if !ok {
		return nil, fmt.Errorf("invalid claims")
	}

	svid := ""
	if v, ok := claims["sub"]; ok {
		svid = v.(string)
	}
	if svid == "" {
		return nil, fmt.Errorf("missing sub (svid) in token")
	}

	service := ""
	if v, ok := claims["service"]; ok {
		service = v.(string)
	}
	group := ""
	if v, ok := claims["group"]; ok {
		group = v.(string)
	}
	role := ""
	if v, ok := claims["role"]; ok {
		role = v.(string)
	}

	logger.S.Debugf("SVID verified: svid=%s service=%s", svid, service)

	return &SVIDClaims{
		SVID:    svid,
		Service: service,
		Group:   group,
		Role:    role,
	}, nil
}

func (v *SVIDVerifier) VerifyFromHeader(authHeader string) (*SVIDClaims, error) {
	if authHeader == "" {
		return nil, fmt.Errorf("missing authorization header")
	}

	parts := strings.SplitN(authHeader, " ", 2)
	if len(parts) != 2 || !strings.EqualFold(parts[0], "Bearer") {
		return nil, fmt.Errorf("invalid authorization header format")
	}

	return v.Verify(context.Background(), parts[1])
}

func ExtractSVIDFromToken(tokenStr string) string {
	token, _, err := new(jwt.Parser).ParseUnverified(tokenStr, jwt.MapClaims{})
	if err != nil {
		return ""
	}
	claims, ok := token.Claims.(jwt.MapClaims)
	if !ok {
		return ""
	}
	if sub, ok := claims["sub"]; ok {
		return sub.(string)
	}
	return ""
}