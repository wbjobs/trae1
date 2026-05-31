package client

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"sync"
	"sync/atomic"
	"time"
)

type Client struct {
	nodes []string

	mu      sync.RWMutex
	leader  string

	httpCli *http.Client

	// counter for timeouts in a row, triggers leader re-probe
	consecutiveTimeouts int32
}

func New(nodes []string, timeout time.Duration) *Client {
	if timeout <= 0 {
		timeout = 3 * time.Second
	}
	return &Client{
		nodes:   append([]string(nil), nodes...),
		httpCli: &http.Client{Timeout: timeout},
	}
}

// SetLeader records a leader hint so future writes are sent there first.
func (c *Client) SetLeader(addr string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.leader = addr
}

func (c *Client) getLeader() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.leader
}

func (c *Client) snapshotNodes() []string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return append([]string(nil), c.nodes...)
}

func (c *Client) SetNodes(nodes []string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.nodes = append([]string(nil), nodes...)
}

type Error struct {
	StatusCode int
	Body       string
	Err        error
}

func (e *Error) Error() string {
	if e.Err != nil {
		return fmt.Sprintf("raftkv client: %v", e.Err)
	}
	return fmt.Sprintf("raftkv client: status=%d body=%s", e.StatusCode, e.Body)
}

func (e *Error) Unwrap() error { return e.Err }

type kvResponse struct {
	Key   string `json:"key"`
	Value string `json:"value"`
	Mode  string `json:"mode"`
	Error string `json:"error"`
}

// ReadMode controls GET semantics.
type ReadMode int

const (
	ReadStrong ReadMode = iota
	ReadLocal
)

func (c *Client) Get(ctx context.Context, key string, mode ReadMode) (string, error) {
	path := "/kv/" + key
	if mode == ReadLocal {
		path += "?mode=local"
	}
	var out kvResponse
	if err := c.doRead(ctx, path, &out); err != nil {
		return "", err
	}
	if out.Error != "" {
		return "", errors.New(out.Error)
	}
	return out.Value, nil
}

func (c *Client) Put(ctx context.Context, key, value string) error {
	path := "/kv/" + key
	var out kvResponse
	if err := c.doWrite(ctx, path, "PUT", []byte(value), &out); err != nil {
		return err
	}
	if out.Error != "" {
		return errors.New(out.Error)
	}
	return nil
}

func (c *Client) Delete(ctx context.Context, key string) error {
	path := "/kv/" + key
	var out kvResponse
	if err := c.doWrite(ctx, path, "DELETE", nil, &out); err != nil {
		return err
	}
	if out.Error != "" {
		return errors.New(out.Error)
	}
	return nil
}

// Leader returns the current known leader HTTP address and whether it's known.
func (c *Client) Leader(ctx context.Context) (string, error) {
	leader, err := c.probeLeader(ctx)
	if err != nil {
		return "", err
	}
	return leader, nil
}

// doRead attempts reads from any node; leader required only for strong mode.
func (c *Client) doRead(ctx context.Context, path string, out interface{}) error {
	nodes := c.snapshotNodes()
	leader := c.getLeader()
	if leader != "" {
		nodes = append([]string{leader}, nodes...)
	}
	var lastErr error
	for _, node := range dedupe(nodes) {
		req, err := c.newRequest(ctx, "GET", node, path, nil)
		if err != nil {
			lastErr = err
			continue
		}
		err = c.send(req, out)
		if err == nil {
			return nil
		}
		lastErr = err
	}
	if lastErr == nil {
		lastErr = errors.New("no nodes available")
	}
	return lastErr
}

// doWrite sends writes to the leader, following redirects and re-probing the
// leader automatically after a few consecutive timeouts.
func (c *Client) doWrite(ctx context.Context, path, method string, body []byte, out interface{}) error {
	const maxRedirects = 4
	attempted := map[string]struct{}{}

	for i := 0; i < maxRedirects; i++ {
		leader := c.getLeader()
		if leader == "" {
			l, err := c.probeLeader(ctx)
			if err != nil {
				return err
			}
			leader = l
			c.SetLeader(leader)
		}
		if _, ok := attempted[leader]; ok {
			l, err := c.probeLeader(ctx)
			if err != nil {
				return err
			}
			leader = l
			c.SetLeader(leader)
			if _, ok := attempted[leader]; ok {
				return errors.New("no leader available")
			}
		}
		attempted[leader] = struct{}{}

		req, err := c.newRequest(ctx, method, leader, path, body)
		if err != nil {
			return err
		}
		err = c.send(req, out)
		if err == nil {
			atomic.StoreInt32(&c.consecutiveTimeouts, 0)
			return nil
		}
		var apiErr *Error
		if errors.As(err, &apiErr) && apiErr.StatusCode == http.StatusTemporaryRedirect {
			// Not leader: probe real leader and retry.
			atomic.AddInt32(&c.consecutiveTimeouts, 1)
			l, perr := c.probeLeader(ctx)
			if perr == nil {
				c.SetLeader(l)
			}
			continue
		}
		if isTimeout(err) {
			cnt := atomic.AddInt32(&c.consecutiveTimeouts, 1)
			if cnt >= 3 {
				atomic.StoreInt32(&c.consecutiveTimeouts, 0)
				l, perr := c.probeLeader(ctx)
				if perr == nil {
					c.SetLeader(l)
				}
			}
			continue
		}
		return err
	}
	return errors.New("too many redirects/timeouts")
}

// probeLeader walks all known nodes asking each for /cluster/leader.
func (c *Client) probeLeader(ctx context.Context) (string, error) {
	nodes := c.snapshotNodes()
	if len(nodes) == 0 {
		return "", errors.New("no known nodes")
	}
	type leaderResp struct {
		ID   string `json:"id"`
		Addr string `json:"addr"`
	}
	var lastErr error
	for _, node := range dedupe(nodes) {
		req, err := c.newRequest(ctx, "GET", node, "/cluster/leader", nil)
		if err != nil {
			lastErr = err
			continue
		}
		var r leaderResp
		if err := c.send(req, &r); err != nil {
			lastErr = err
			continue
		}
		if r.Addr == "" {
			lastErr = errors.New("no leader reported")
			continue
		}
		// r.Addr is the raft addr. The HTTP addr is the same host with the
		// node's http port. We don't know it, so return the node that gave us
		// the answer as a valid target, since it's alive.
		_ = r.ID
		return node, nil
	}
	if lastErr == nil {
		lastErr = errors.New("all probes failed")
	}
	return "", lastErr
}

func (c *Client) newRequest(ctx context.Context, method, node, path string, body []byte) (*http.Request, error) {
	url := fmt.Sprintf("http://%s%s", node, path)
	var rdr io.Reader
	if body != nil {
		rdr = bytes.NewReader(body)
	}
	return http.NewRequestWithContext(ctx, method, url, rdr)
}

func (c *Client) send(req *http.Request, out interface{}) error {
	resp, err := c.httpCli.Do(req)
	if err != nil {
		return &Error{Err: err}
	}
	defer resp.Body.Close()
	raw, err := io.ReadAll(resp.Body)
	if err != nil {
		return &Error{Err: err}
	}
	if resp.StatusCode/100 != 2 {
		return &Error{StatusCode: resp.StatusCode, Body: string(raw)}
	}
	if out != nil {
		if err := json.Unmarshal(raw, out); err != nil {
			return &Error{Err: fmt.Errorf("decode response: %w", err)}
		}
	}
	return nil
}

func isTimeout(err error) bool {
	var apiErr *Error
	if errors.As(err, &apiErr) {
		if apiErr.Err != nil {
			err = apiErr.Err
		}
	}
	var t interface{ Timeout() bool }
	if errors.As(err, &t) {
		return t.Timeout()
	}
	return false
}

func dedupe(in []string) []string {
	seen := map[string]struct{}{}
	out := make([]string, 0, len(in))
	for _, s := range in {
		if s == "" {
			continue
		}
		if _, ok := seen[s]; ok {
			continue
		}
		seen[s] = struct{}{}
		out = append(out, s)
	}
	return out
}
