package identity

import (
	"context"
	"fmt"
	"time"

	"github.com/spiffe/go-spiffe/v2/svid/jwtsvid"
	"github.com/spiffe/go-spiffe/v2/workloadapi"
)

type JWTValidator struct {
	source *workloadapi.JWTSource
}

func NewJWTValidator(ctx context.Context, socketPath string) (*JWTValidator, error) {
	source, err := workloadapi.NewJWTSource(ctx, workloadapi.WithAddr("unix://"+socketPath))
	if err != nil {
		return nil, fmt.Errorf("create jwt source: %w", err)
	}
	return &JWTValidator{source: source}, nil
}

func (v *JWTValidator) VerifyJWT(token string) (string, error) {
	svid, err := jwtsvid.ParseAndValidate(token, v.source, []string{"*"})
	if err != nil {
		return "", fmt.Errorf("jwt validate: %w", err)
	}
	return svid.ID.String(), nil
}

func (v *JWTValidator) Close() error { return nil }

func RemainingLifetime(issuedAt, expiresAt time.Time, now time.Time) float64 {
	total := expiresAt.Sub(issuedAt).Seconds()
	if total <= 0 {
		return 0
	}
	remaining := expiresAt.Sub(now).Seconds()
	if remaining < 0 {
		return 0
	}
	return remaining / total
}
