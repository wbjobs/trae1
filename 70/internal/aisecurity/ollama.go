package aisecurity

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"time"
)

type OllamaClient struct {
	baseURL    string
	model      string
	httpClient *http.Client
}

type OllamaGenerateRequest struct {
	Model  string `json:"model"`
	Prompt string `json:"prompt"`
	Stream bool   `json:"stream"`
}

type OllamaGenerateResponse struct {
	Model              string    `json:"model"`
	CreatedAt          time.Time `json:"created_at"`
	Response           string    `json:"response"`
	Done               bool      `json:"done"`
	TotalDuration      int64     `json:"total_duration"`
	LoadDuration       int64     `json:"load_duration"`
	PromptEvalDuration int64     `json:"prompt_eval_duration"`
	EvalCount          int       `json:"eval_count"`
	EvalDuration       int64     `json:"eval_duration"`
}

type SecurityAssessment struct {
	Score    int      `json:"score"`
	Reasons  []string `json:"reasons"`
	Category string   `json:"category"`
}

func NewOllamaClient(baseURL, model string, timeout time.Duration) *OllamaClient {
	return &OllamaClient{
		baseURL: baseURL,
		model:   model,
		httpClient: &http.Client{
			Timeout: timeout,
		},
	}
}

func (c *OllamaClient) Check(ctx context.Context, command string, args []string) (*SecurityAssessment, error) {
	fullCommand := command
	if len(args) > 0 {
		fullCommand = command + " " + joinArgs(args)
	}

	prompt := buildPrompt(fullCommand)

	reqBody := OllamaGenerateRequest{
		Model:  c.model,
		Prompt: prompt,
		Stream: false,
	}

	jsonBody, err := json.Marshal(reqBody)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal request: %w", err)
	}

	req, err := http.NewRequestWithContext(ctx, "POST", c.baseURL+"/api/generate", bytes.NewReader(jsonBody))
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("ollama request failed: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("ollama returned status %d: %s", resp.StatusCode, string(body))
	}

	var ollamaResp OllamaGenerateResponse
	if err := json.Unmarshal(body, &ollamaResp); err != nil {
		return nil, fmt.Errorf("failed to unmarshal response: %w", err)
	}

	return parseAssessment(ollamaResp.Response)
}

func buildPrompt(command string) string {
	return fmt.Sprintf(`You are a security expert analyzing Linux commands for potential malicious behavior.

Analyze the following command for security risks:
COMMAND: %s

Respond with ONLY a JSON object (no markdown, no other text) with this structure:
{
  "score": <0-100>,
  "reasons": ["reason1", "reason2"],
  "category": "<safe|suspicious|dangerous>"
}

Scoring guidelines:
- 90-100: Completely safe (e.g., ffmpeg conversion, image resize, pdftotext)
- 75-89: Low risk (normal use with caution needed)
- 60-74: Suspicious (unusual patterns, potential data exfiltration)
- 40-59: High risk (system modification, privilege escalation attempts)
- 0-39: Critical danger (rm -rf /, fork bombs, curl|bash, reverse shells)

Be strict. If in doubt, give a lower score. Only safe, standard use of tools like ffmpeg, convert, pdftotext should score 90+.`, command)
}

func joinArgs(args []string) string {
	result := ""
	for i, arg := range args {
		if i > 0 {
			result += " "
		}
		result += arg
	}
	return result
}
