package ai

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

type OllamaRequest struct {
	Model  string `json:"model"`
	Prompt string `json:"prompt"`
	Stream bool   `json:"stream"`
	Format string `json:"format,omitempty"`
	System string `json:"system,omitempty"`
}

type OllamaResponse struct {
	Model              string    `json:"model"`
	CreatedAt          time.Time `json:"created_at"`
	Response           string    `json:"response"`
	Done               bool      `json:"done"`
	TotalDuration      int64     `json:"total_duration"`
	LoadDuration       int64     `json:"load_duration"`
	PromptEvalCount    int       `json:"prompt_eval_count"`
	PromptEvalDuration int64     `json:"prompt_eval_duration"`
	EvalCount          int       `json:"eval_count"`
	EvalDuration       int64     `json:"eval_duration"`
}

type CommandAnalysis struct {
	Command        string `json:"command"`
	Intent         string `json:"intent"`
	Category       string `json:"category"`
	RiskLevel      int    `json:"risk_level"`
	RiskReason     string `json:"risk_reason"`
	ImpactScope    string `json:"impact_scope"`
	RequiresApproval bool `json:"requires_approval"`
	Suggestion     string `json:"suggestion"`
}

type RiskAnalysisResult struct {
	AnalyzedAt  time.Time         `json:"analyzed_at"`
	Model       string            `json:"model"`
	Analyses    []CommandAnalysis `json:"analyses"`
	OverallRisk int               `json:"overall_risk"`
	Summary     string            `json:"summary"`
}

const systemPrompt = `You are a security audit expert for SSH session command analysis. Analyze each command and assess its risk level.

Risk levels (1-5):
1 - Low: Normal operations (ls, cd, pwd, cat, echo, grep, etc.)
2 - Medium: Informational/config commands (ps, top, netstat, ifconfig, vi, vim, nano)
3 - High: Modifying commands (chmod, chown, rm, mv, cp to system dirs, service restart)
4 - Critical: Dangerous system commands (rm -rf /, mkfs, dd, fdisk, iptables -F, shutdown, reboot)
5 - Severe: Data destruction/backdoor commands (DROP DATABASE, TRUNCATE TABLE, format c:, curl|bash, wget|sh)

For each command, provide:
- intent: What the command is trying to do
- category: filesystem, network, process, database, privilege, destructive, other
- risk_level: 1-5
- risk_reason: Why this is risky
- impact_scope: system, user, data, network, other
- requires_approval: true if risk_level >= 3
- suggestion: Safer alternative or warning

Respond with valid JSON array of objects, one per command. No markdown, no explanation outside JSON.`

func NewClient(baseURL, model string, timeout time.Duration) *OllamaClient {
	if timeout == 0 {
		timeout = 30 * time.Second
	}
	return &OllamaClient{
		baseURL: baseURL,
		model:   model,
		httpClient: &http.Client{
			Timeout: timeout,
		},
	}
}

func NewOllamaClient(baseURL, model string) *OllamaClient {
	return NewClient(baseURL, model, 30*time.Second)
}

func (c *OllamaClient) AnalyzeCommands(ctx context.Context, commands []string) (*RiskAnalysisResult, error) {
	if len(commands) == 0 {
		return &RiskAnalysisResult{
			AnalyzedAt: time.Now(),
			Model:      c.model,
			Analyses:   []CommandAnalysis{},
			OverallRisk: 1,
			Summary:    "No commands to analyze",
		}, nil
	}

	prompt := buildPrompt(commands)

	req := OllamaRequest{
		Model:  c.model,
		Prompt: prompt,
		Stream: false,
		System: systemPrompt,
		Format: "json",
	}

	body, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("marshal request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, "POST", c.baseURL+"/api/generate", bytes.NewReader(body))
	if err != nil {
		return nil, fmt.Errorf("create request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	resp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("ollama request: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read response: %w", err)
	}

	var ollamaResp OllamaResponse
	if err := json.Unmarshal(respBody, &ollamaResp); err != nil {
		return nil, fmt.Errorf("unmarshal response: %w\nraw: %s", err, string(respBody))
	}

	result, err := parseAnalysis(ollamaResp.Response, commands)
	if err != nil {
		return nil, fmt.Errorf("parse analysis: %w\nraw response: %s", err, ollamaResp.Response)
	}

	result.AnalyzedAt = time.Now()
	result.Model = c.model

	return result, nil
}

func buildPrompt(commands []string) string {
	prompt := "Analyze these SSH session commands for security risk:\n\n"
	for i, cmd := range commands {
		prompt += fmt.Sprintf("%d. %s\n", i+1, cmd)
	}
	prompt += "\nReturn a JSON array with analysis for each command."
	return prompt
}

func parseAnalysis(response string, commands []string) (*RiskAnalysisResult, error) {
	response = cleanJSONResponse(response)

	var analyses []CommandAnalysis
	if err := json.Unmarshal([]byte(response), &analyses); err != nil {
		analyses = []CommandAnalysis{}
		for _, cmd := range commands {
			analyses = append(analyses, CommandAnalysis{
				Command:            cmd,
				Intent:             "unknown",
				Category:           "other",
				RiskLevel:          2,
				RiskReason:         "AI analysis failed, manual review recommended",
				ImpactScope:        "unknown",
				RequiresApproval:   true,
				Suggestion:         "Review command before execution",
			})
		}
	}

	for i := range analyses {
		if i < len(commands) {
			analyses[i].Command = commands[i]
		}
		if len(analyses) > len(commands) {
			analyses = analyses[:len(commands)]
			break
		}
		if len(analyses) < len(commands) {
			for j := len(analyses); j < len(commands); j++ {
				analyses = append(analyses, CommandAnalysis{
					Command:          commands[j],
					Intent:           "not_analyzed",
					Category:         "other",
					RiskLevel:        2,
					RequiresApproval: true,
					Suggestion:       "Not analyzed by AI, review manually",
				})
			}
		}
	}

	overall := 1
	for _, a := range analyses {
		if a.RiskLevel > overall {
			overall = a.RiskLevel
		}
	}

	summary := generateSummary(analyses, overall)

	return &RiskAnalysisResult{
		Analyses:    analyses,
		OverallRisk: overall,
		Summary:     summary,
	}, nil
}

func cleanJSONResponse(response string) string {
	if len(response) == 0 {
		return "[]"
	}

	if response[0] == '[' {
		return response
	}

	start := 0
	for i, c := range response {
		if c == '[' {
			start = i
			break
		}
	}
	if start == 0 && response[0] != '[' {
		return "[]"
	}

	end := len(response)
	for i := len(response) - 1; i >= 0; i-- {
		if response[i] == ']' {
			end = i + 1
			break
		}
	}

	return response[start:end]
}

func generateSummary(analyses []CommandAnalysis, overall int) string {
	var highRiskCount, criticalCount int
	for _, a := range analyses {
		if a.RiskLevel >= 4 {
			criticalCount++
		} else if a.RiskLevel >= 3 {
			highRiskCount++
		}
	}

	level := "SAFE"
	switch overall {
	case 2:
		level = "NORMAL"
	case 3:
		level = "ELEVATED"
	case 4:
		level = "DANGEROUS"
	case 5:
		level = "CRITICAL"
	}

	return fmt.Sprintf("Risk Level: %s | Total: %d | High Risk: %d | Critical: %d",
		level, len(analyses), highRiskCount, criticalCount)
}

func (r *RiskAnalysisResult) GetCommandsRequiringApproval() []CommandAnalysis {
	var result []CommandAnalysis
	for _, a := range r.Analyses {
		if a.RequiresApproval || a.RiskLevel >= 3 {
			result = append(result, a)
		}
	}
	return result
}

func (r *RiskAnalysisResult) GetHighestRiskCommand() *CommandAnalysis {
	if len(r.Analyses) == 0 {
		return nil
	}
	highest := &r.Analyses[0]
	for i := range r.Analyses {
		if r.Analyses[i].RiskLevel > highest.RiskLevel {
			highest = &r.Analyses[i]
		}
	}
	return highest
}
