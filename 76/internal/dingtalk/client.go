package dingtalk

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sync"
	"time"
)

type Client struct {
	appKey    string
	appSecret string
	agentID   string
	baseURL   string
	token     string
	tokenExp  time.Time
	mu        sync.Mutex
	httpCli   *http.Client
}

type TokenResponse struct {
	AccessToken string `json:"accessToken"`
	ExpiresIn   int    `json:"expireIn"`
}

type ApprovalRequest struct {
	ProcessCode           string              `json:"processCode"`
	OriginatorUserID      string              `json:"originatorUserId"`
	DeptID                int                 `json:"deptId"`
	Approvers             string              `json:"approvers"`
	CcList                string              `json:"ccList,omitempty"`
	CcPosition            string              `json:"ccPosition,omitempty"`
	FormComponentValues   []FormComponent     `json:"formComponentValues"`
	ApproversV2           []ApproverNode      `json:"approversV2,omitempty"`
}

type FormComponent struct {
	Name  string `json:"name"`
	Value string `json:"value"`
}

type ApproverNode struct {
	Approvers []string `json:"approvers"`
}

type ApprovalResponse struct {
	Code    int    `json:"code"`
	Msg     string `json:"msg"`
	Result  string `json:"result"`
}

type ApprovalInstanceResponse struct {
	Code   int               `json:"code"`
	Msg    string            `json:"msg"`
	Result *ApprovalInstance `json:"result"`
}

type ApprovalInstance struct {
	Title            string        `json:"title"`
	Status           string        `json:"status"`
	Result           string        `json:"result"`
	Approvers        []ApproverInfo `json:"approvers"`
	CreateTime       int64         `json:"createTime"`
	FinishTime       int64         `json:"finishTime,omitempty"`
	FormComponentValues []FormComponentValue `json:"formComponentValues"`
	OperationRecords []OperationRecord `json:"operationRecords"`
}

type ApproverInfo struct {
	UserID   string `json:"userId"`
	TaskID   string `json:"taskId"`
	Status   string `json:"status"`
	Result   string `json:"result"`
}

type FormComponentValue struct {
	Name  string `json:"name"`
	Value string `json:"value"`
}

type OperationRecord struct {
	UserID     string `json:"userId"`
	Date       int64  `json:"date"`
	OperationType string `json:"operationType"`
	OperationResult string `json:"operationResult"`
	Remark     string `json:"remark"`
}

const (
	ApprovalStatusPending = "PENDING"
	ApprovalStatusRunning = "RUNNING"
	ApprovalStatusFinish  = "FINISH"
	ApprovalStatusTerminate = "TERMINATE"

	ApprovalResultPass    = "agree"
	ApprovalResultReject  = "refuse"

	DefaultBaseURL = "https://oapi.dingtalk.com"
)

func NewClient(appKey, appSecret, agentID string) *Client {
	return &Client{
		appKey:    appKey,
		appSecret: appSecret,
		agentID:   agentID,
		baseURL:   DefaultBaseURL,
		httpCli:   &http.Client{Timeout: 15 * time.Second},
	}
}

func NewClientWithBaseURL(appKey, appSecret, agentID, baseURL string) *Client {
	if baseURL == "" {
		baseURL = DefaultBaseURL
	}
	return &Client{
		appKey:    appKey,
		appSecret: appSecret,
		agentID:   agentID,
		baseURL:   baseURL,
		httpCli:   &http.Client{Timeout: 15 * time.Second},
	}
}

func (c *Client) GetToken(ctx context.Context) (string, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.token != "" && time.Now().Before(c.tokenExp) {
		return c.token, nil
	}

	reqURL := fmt.Sprintf("%s/gettoken?appkey=%s&appsecret=%s", c.baseURL, c.appKey, c.appSecret)
	req, err := http.NewRequestWithContext(ctx, "GET", reqURL, nil)
	if err != nil {
		return "", fmt.Errorf("create token request: %w", err)
	}

	resp, err := c.httpCli.Do(req)
	if err != nil {
		return "", fmt.Errorf("token request: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", fmt.Errorf("read token response: %w", err)
	}

	var tokenResp TokenResponse
	if err := json.Unmarshal(body, &tokenResp); err != nil {
		return "", fmt.Errorf("unmarshal token: %w", err)
	}

	if tokenResp.AccessToken == "" {
		return "", fmt.Errorf("empty access token, response: %s", string(body))
	}

	c.token = tokenResp.AccessToken
	c.tokenExp = time.Now().Add(time.Duration(tokenResp.ExpiresIn-300) * time.Second)
	return c.token, nil
}

func (c *Client) CreateApproval(ctx context.Context, req ApprovalRequest) (string, error) {
	token, err := c.GetToken(ctx)
	if err != nil {
		return "", fmt.Errorf("get token: %w", err)
	}

	body, err := json.Marshal(req)
	if err != nil {
		return "", fmt.Errorf("marshal request: %w", err)
	}

	reqURL := fmt.Sprintf("%s/topapi/processinstance/create?access_token=%s", c.baseURL, token)
	httpReq, err := http.NewRequestWithContext(ctx, "POST", reqURL, bytes.NewReader(body))
	if err != nil {
		return "", fmt.Errorf("create request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	resp, err := c.httpCli.Do(httpReq)
	if err != nil {
		return "", fmt.Errorf("approval request: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", fmt.Errorf("read response: %w", err)
	}

	var approvalResp ApprovalResponse
	if err := json.Unmarshal(respBody, &approvalResp); err != nil {
		return "", fmt.Errorf("unmarshal response: %w\nraw: %s", err, string(respBody))
	}

	if approvalResp.Code != 0 {
		return "", fmt.Errorf("dingtalk error: code=%d, msg=%s", approvalResp.Code, approvalResp.Msg)
	}

	return approvalResp.Result, nil
}

func (c *Client) GetApprovalInstance(ctx context.Context, processInstanceID string) (*ApprovalInstance, error) {
	token, err := c.GetToken(ctx)
	if err != nil {
		return nil, fmt.Errorf("get token: %w", err)
	}

	reqBody := map[string]string{
		"processInstanceId": processInstanceID,
	}
	body, _ := json.Marshal(reqBody)

	reqURL := fmt.Sprintf("%s/topapi/processinstance/get?access_token=%s", c.baseURL, token)
	httpReq, err := http.NewRequestWithContext(ctx, "POST", reqURL, bytes.NewReader(body))
	if err != nil {
		return nil, fmt.Errorf("create request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	resp, err := c.httpCli.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("instance request: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read response: %w", err)
	}

	var instResp ApprovalInstanceResponse
	if err := json.Unmarshal(respBody, &instResp); err != nil {
		return nil, fmt.Errorf("unmarshal response: %w\nraw: %s", err, string(respBody))
	}

	if instResp.Code != 0 {
		return nil, fmt.Errorf("dingtalk error: code=%d, msg=%s", instResp.Code, instResp.Msg)
	}

	return instResp.Result, nil
}

func (c *Client) SendRobotMessage(ctx context.Context, webhookURL, secret string, msg map[string]interface{}) error {
	body, err := json.Marshal(msg)
	if err != nil {
		return fmt.Errorf("marshal message: %w", err)
	}

	req, err := http.NewRequestWithContext(ctx, "POST", webhookURL, bytes.NewReader(body))
	if err != nil {
		return fmt.Errorf("create request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")

	resp, err := c.httpCli.Do(req)
	if err != nil {
		return fmt.Errorf("send message: %w", err)
	}
	defer resp.Body.Close()

	_, _ = io.ReadAll(resp.Body)
	return nil
}

func BuildCommandApprovalForm(sessionID, user, targetHost string, commands []string, riskLevel int) []FormComponent {
	form := []FormComponent{
		{Name: "会话ID", Value: sessionID},
		{Name: "操作用户", Value: user},
		{Name: "目标主机", Value: targetHost},
		{Name: "风险等级", Value: fmt.Sprintf("%d/5", riskLevel)},
		{Name: "审批原因", Value: "检测到高风险命令需要审批"},
	}

	var cmdList string
	for i, cmd := range commands {
		cmdList += fmt.Sprintf("%d. %s\n", i+1, cmd)
	}
	form = append(form, FormComponent{Name: "待审批命令", Value: cmdList})

	return form
}
