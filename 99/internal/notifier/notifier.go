package notifier

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/tenantnfs/quotad/internal/config"
	"github.com/tenantnfs/quotad/internal/model"
)

type Notifier struct {
	cfg *config.Config
	log *log.Logger

	mu       sync.Mutex
	lastSent map[string]time.Time
}

func New(cfg *config.Config) *Notifier {
	return &Notifier{
		cfg:      cfg,
		log:      log.New(os.Stderr, "[notifier] ", log.LstdFlags),
		lastSent: make(map[string]time.Time),
	}
}

func (n *Notifier) Notify(ctx context.Context, ev *model.AlertEvent) error {
	if n.cfg.WebhookURL == "" {
		n.log.Printf("webhook URL not configured, skip notification for tenant %s", ev.TenantID)
		return nil
	}

	// Throttle: at most one alert per tenant per 5 minutes.
	n.mu.Lock()
	key := fmt.Sprintf("%s:%s", ev.TenantID, ev.Type)
	if last, ok := n.lastSent[key]; ok && time.Since(last) < 5*time.Minute {
		n.mu.Unlock()
		return nil
	}
	n.lastSent[key] = time.Now()
	n.mu.Unlock()

	body, err := json.Marshal(ev)
	if err != nil {
		return err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, n.cfg.WebhookURL, bytes.NewReader(body))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode >= 300 {
		return fmt.Errorf("webhook returned status %d", resp.StatusCode)
	}
	n.log.Printf("sent alert to tenant %s type=%s ratio=%.2f", ev.TenantID, ev.Type, ev.Ratio)
	return nil
}
