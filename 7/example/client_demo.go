package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"time"

	"api-signature/util"
)

const (
	clientID     = "client_web_001"
	clientSecret = "secret_web_abc123xyz789"
	apiBaseURL   = "http://localhost:8080"
)

type ApiRequest struct {
	ClientID  string `json:"client_id"`
	Timestamp int64  `json:"timestamp"`
	Nonce     string `json:"nonce"`
	Signature string `json:"signature"`
	Data      string `json:"data,omitempty"`
}

func main() {
	fmt.Println("=== API Signature Client Demo ===")
	fmt.Println()

	timestamp := util.GetCurrentTimestamp()
	nonce := util.GenerateNonce()

	params := map[string]string{}
	method := "GET"
	path := "/api/v1/data/read"
	body := ""

	signature := util.GenerateSignature(
		clientSecret,
		params,
		method,
		path,
		body,
		timestamp,
		nonce,
	)

	fmt.Printf("Client ID: %s\n", clientID)
	fmt.Printf("Timestamp: %d\n", timestamp)
	fmt.Printf("Nonce: %s\n", nonce)
	fmt.Printf("Signature: %s\n", signature)
	fmt.Println()

	requestData := ApiRequest{
		ClientID:  clientID,
		Timestamp: timestamp,
		Nonce:     nonce,
		Signature: signature,
	}

	jsonData, _ := json.Marshal(requestData)
	fmt.Printf("Request JSON: %s\n", string(jsonData))
	fmt.Println()

	req, err := http.NewRequest(method, apiBaseURL+path, bytes.NewBuffer(jsonData))
	if err != nil {
		fmt.Printf("Error creating request: %v\n", err)
		return
	}
	req.Header.Set("Content-Type", "application/json")

	client := &http.Client{Timeout: 10 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		fmt.Printf("Error sending request: %v\n", err)
		return
	}
	defer resp.Body.Close()

	respBody, _ := io.ReadAll(resp.Body)
	fmt.Printf("Response Status: %d\n", resp.StatusCode)
	fmt.Printf("Response Body: %s\n", string(respBody))
}
