package service

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"sort"
	"strings"
	"time"

	"api-signature/config"
	"api-signature/model"
	"api-signature/repository"
	"api-signature/util"
)

type SignatureService struct{}

func NewSignatureService() *SignatureService {
	return &SignatureService{}
}

func (s *SignatureService) GenerateSignature(clientID string, params map[string]string, method string, path string, body string) (*model.SignatureData, string, error) {
	clientConfig := config.GetClientConfig(clientID)
	if clientConfig == nil {
		return nil, "", fmt.Errorf("client not found: %s", clientID)
	}

	timestamp := util.GetCurrentTimestamp()
	nonce := util.GenerateNonce()

	signature := util.GenerateSignature(
		clientConfig.ClientSecret,
		params,
		method,
		path,
		body,
		timestamp,
		nonce,
	)

	sigData := &model.SignatureData{
		ClientID:   clientID,
		Timestamp:  timestamp,
		Nonce:      nonce,
		Method:     method,
		Path:       path,
		BodyHash:   util.MD5Hash(body),
		ParamsHash: util.MD5Hash(util.BuildSignString(convertParamsToInterface(params))),
	}

	expiration := time.Duration(config.AppConfig.Security.SignatureExpire) * time.Second
	if err := repository.SetSignatureNonce(nonce, clientID, expiration); err != nil {
		return nil, "", fmt.Errorf("failed to store nonce: %w", err)
	}

	return sigData, signature, nil
}

func (s *SignatureService) VerifySignature(clientID string, signature string, params map[string]string, method string, path string, body string, timestamp int64, nonce string) error {
	clientConfig := config.GetClientConfig(clientID)
	if clientConfig == nil {
		return fmt.Errorf("client not found")
	}

	if err := s.DetectParameterTampering(params, body); err != nil {
		statsService := NewStatsService()
		statsService.RecordError("tampering", clientID, "")
		return fmt.Errorf("parameter tampering detected: %w", err)
	}

	valid, err := s.verifyWithSecretRotation(clientID, signature, params, method, path, body, timestamp, nonce)
	if err != nil {
		return err
	}

	if valid {
		return nil
	}

	if util.VerifySignature(signature, clientConfig.ClientSecret, params, method, path, body, timestamp, nonce) {
		return nil
	}

	return fmt.Errorf("signature verification failed")
}

func (s *SignatureService) verifyWithSecretRotation(clientID string, signature string, params map[string]string, method string, path string, body string, timestamp int64, nonce string) (bool, error) {
	secretRotationService := NewSecretRotationService()
	
	valid, err := secretRotationService.ValidateSecretWithHistory(
		clientID, signature, params, method, path, body, timestamp, nonce,
	)
	if err != nil {
		return false, nil
	}
	
	return valid, nil
}

func (s *SignatureService) DetectParameterTampering(params map[string]string, body string) error {
	if params != nil {
		for key, value := range params {
			if strings.Contains(key, "'") || strings.Contains(key, "\"") || 
			   strings.Contains(key, "<") || strings.Contains(key, ">") {
				return fmt.Errorf("suspicious characters in parameter key: %s", key)
			}
			
			if len(value) > 10000 {
				return fmt.Errorf("parameter value too long: %s", key)
			}
			
			if strings.Contains(value, "javascript:") || 
			   strings.Contains(value, "<script") ||
			   strings.Contains(value, "onerror=") ||
			   strings.Contains(value, "onload=") {
				return fmt.Errorf("potential XSS in parameter: %s", key)
			}
			
			if strings.Contains(value, "' OR ") || 
			   strings.Contains(value, "\" OR ") ||
			   strings.Contains(value, "1=1") ||
			   strings.Contains(value, "DROP TABLE") {
				return fmt.Errorf("potential SQL injection in parameter: %s", key)
			}
		}
	}

	if body != "" {
		if len(body) > 1048576 {
			return fmt.Errorf("request body too large")
		}
		
		bodyLower := strings.ToLower(body)
		if strings.Contains(bodyLower, "<script") || 
		   strings.Contains(bodyLower, "javascript:") {
			return fmt.Errorf("potential XSS in request body")
		}
		
		if strings.Contains(bodyLower, "drop table") ||
		   strings.Contains(bodyLower, "union select") ||
		   strings.Contains(bodyLower, "or 1=1") {
			return fmt.Errorf("potential SQL injection in request body")
		}
	}

	return nil
}

func (s *SignatureService) ValidateTimestamp(timestamp int64) error {
	if timestamp <= 0 {
		return fmt.Errorf("invalid timestamp value: %d", timestamp)
	}

	tolerance := config.AppConfig.Security.TimestampTolerance
	if tolerance <= 0 {
		tolerance = 300
	}

	currentTime := util.GetCurrentTimestamp()
	diff := currentTime - timestamp

	if diff < 0 {
		diff = -diff
	}

	if diff > int64(tolerance) {
		return fmt.Errorf("timestamp out of tolerance, diff: %d seconds, tolerance: %d seconds, server_time: %d, client_time: %d",
			diff, tolerance, currentTime, timestamp)
	}

	return nil
}

func (s *SignatureService) CheckNonceReplay(nonce string, clientID string) error {
	exists, err := repository.CheckNonceExists(nonce)
	if err != nil {
		return fmt.Errorf("failed to check nonce: %w", err)
	}

	if exists {
		return fmt.Errorf("nonce replay detected")
	}

	nonceExpire := time.Duration(config.AppConfig.Security.NonceExpire) * time.Second
	if err := repository.SetSignatureNonce(nonce, clientID, nonceExpire); err != nil {
		return fmt.Errorf("failed to store nonce: %w", err)
	}

	return nil
}

func (s *SignatureService) StoreRequestSignature(clientID string, signature string) error {
	expiration := time.Duration(config.AppConfig.Security.SignatureExpire) * time.Second
	return repository.SetRequestSignature(clientID, signature, expiration)
}

func (s *SignatureService) CheckRequestSignature(clientID string, signature string) (bool, error) {
	return repository.CheckRequestSignature(clientID, signature)
}

func (s *SignatureService) CalculateParamsHash(params map[string]string) string {
	if len(params) == 0 {
		return ""
	}

	var keys []string
	for k := range params {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	var hashInput strings.Builder
	for _, k := range keys {
		hashInput.WriteString(k)
		hashInput.WriteString("=")
		hashInput.WriteString(params[k])
		hashInput.WriteString("&")
	}

	h := sha256.New()
	h.Write([]byte(hashInput.String()))
	return hex.EncodeToString(h.Sum(nil))
}

func convertParamsToInterface(params map[string]string) map[string]interface{} {
	result := make(map[string]interface{})
	for k, v := range params {
		result[k] = v
	}
	return result
}
