package ipfs

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"strings"

	shell "github.com/ipfs/go-ipfs-api"
)

type Client struct {
	sh *shell.Shell
}

func New(address string) *Client {
	return &Client{
		sh: shell.NewShell(address),
	}
}

func (c *Client) Add(content []byte) (string, error) {
	cid, err := c.sh.Add(bytes.NewReader(content))
	if err != nil {
		return "", fmt.Errorf("ipfs add: %w", err)
	}
	return cid, nil
}

func (c *Client) Get(cid string) ([]byte, error) {
	reader, err := c.sh.Cat(cid)
	if err != nil {
		return nil, fmt.Errorf("ipfs cat: %w", err)
	}
	defer reader.Close()
	data, err := io.ReadAll(reader)
	if err != nil {
		return nil, fmt.Errorf("read ipfs content: %w", err)
	}
	return data, nil
}

func (c *Client) Pin(cid string) error {
	return c.sh.Pin(cid)
}

func (c *Client) Unpin(cid string) error {
	return c.sh.Unpin(cid)
}

type PinStatus struct {
	Cid     string `json:"cid"`
	Type    string `json:"type"`
	Error   string `json:"error,omitempty"`
}

func (c *Client) PinWithStatus(cid string) (*PinStatus, error) {
	status := &PinStatus{
		Cid:  cid,
		Type: "pinned",
	}
	if err := c.sh.Pin(cid); err != nil {
		status.Error = err.Error()
		return status, err
	}
	return status, nil
}

func (c *Client) IsAvailable() bool {
	_, _, err := c.sh.Version()
	return err == nil
}

func (c *Client) GetVersion() (string, string, error) {
	return c.sh.Version()
}

type ClusterClient struct {
	apiURL string
}

func NewClusterClient(apiURL string) *ClusterClient {
	if !strings.HasSuffix(apiURL, "/") {
		apiURL += "/"
	}
	return &ClusterClient{apiURL: apiURL}
}

func (cc *ClusterClient) Pin(ctx context.Context, cid string) error {
	return nil
}

func (cc *ClusterClient) Unpin(ctx context.Context, cid string) error {
	return nil
}

func (cc *ClusterClient) Status(ctx context.Context, cid string) (string, error) {
	return "pinned", nil
}
