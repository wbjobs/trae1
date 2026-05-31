package service

import (
	"fmt"
	"sync"
	"time"

	"api-signature/config"
	"api-signature/model"
	"api-signature/repository"
	"api-signature/util"
)

type SecretRotationService struct {
	mu sync.RWMutex
}

var secretRotationServiceInstance *SecretRotationService
var secretRotationServiceOnce sync.Once

func NewSecretRotationService() *SecretRotationService {
	secretRotationServiceOnce.Do(func() {
		secretRotationServiceInstance = &SecretRotationService{}
	})
	return secretRotationServiceInstance
}

func (s *SecretRotationService) RotateSecret(clientID string, newSecret string, gracePeriodHours int) (*model.SecretVersion, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	clientConfig := config.GetClientConfig(clientID)
	if clientConfig == nil {
		return nil, fmt.Errorf("client not found: %s", clientID)
	}

	history, err := repository.GetSecretHistory(clientID)
	if err != nil {
		return nil, fmt.Errorf("failed to get secret history: %w", err)
	}

	currentVersion := 1
	if len(history) > 0 {
		currentVersion = history[len(history)-1].Version + 1
	}

	if newSecret == "" {
		newSecret = s.generateNewSecret()
	}

	if gracePeriodHours <= 0 {
		gracePeriodHours = 24
	}

	gracePeriod := time.Duration(gracePeriodHours) * time.Hour

	newVersion := model.SecretVersion{
		Version:   currentVersion,
		Secret:    newSecret,
		CreatedAt: time.Now(),
		ExpiresAt: time.Now().Add(gracePeriod),
		IsActive:  true,
	}

	if len(history) > 0 {
		history[len(history)-1].IsActive = false
		history[len(history)-1].ExpiresAt = time.Now().Add(gracePeriod)
	}

	history = append(history, newVersion)

	if err := repository.SetSecretHistory(clientID, history); err != nil {
		return nil, fmt.Errorf("failed to save secret history: %w", err)
	}

	if err := repository.SetClientSecret(clientID, newSecret, currentVersion, 0); err != nil {
		return nil, fmt.Errorf("failed to update current secret: %w", err)
	}

	clientService := NewClientService()
	if err := clientService.UpdateClientSecret(clientID, newSecret, currentVersion); err != nil {
		return nil, fmt.Errorf("failed to update client secret in memory: %w", err)
	}

	return &newVersion, nil
}

func (s *SecretRotationService) GetSecretHistory(clientID string) ([]model.SecretVersion, error) {
	history, err := repository.GetSecretHistory(clientID)
	if err != nil {
		return nil, err
	}
	return history, nil
}

func (s *SecretRotationService) GetActiveSecret(clientID string) (string, int, error) {
	secret, version, err := repository.GetClientSecret(clientID)
	if err != nil {
		return "", 0, err
	}

	if secret != "" {
		return secret, version, nil
	}

	clientConfig := config.GetClientConfig(clientID)
	if clientConfig != nil {
		return clientConfig.ClientSecret, 1, nil
	}

	return "", 0, fmt.Errorf("no active secret found for client: %s", clientID)
}

func (s *SecretRotationService) ValidateSecretWithHistory(clientID string, signature string, params map[string]string, method string, path string, body string, timestamp int64, nonce string) (bool, error) {
	activeSecret, _, err := s.GetActiveSecret(clientID)
	if err != nil {
		return false, err
	}

	if util.VerifySignature(signature, activeSecret, params, method, path, body, timestamp, nonce) {
		return true, nil
	}

	history, err := repository.GetSecretHistory(clientID)
	if err != nil {
		return false, err
	}

	now := time.Now()
	for _, version := range history {
		if !version.IsActive && version.ExpiresAt.After(now) {
			continue
		}
		if util.VerifySignature(signature, version.Secret, params, method, path, body, timestamp, nonce) {
			return true, nil
		}
	}

	return false, nil
}

func (s *SecretRotationService) CleanupExpiredSecrets() error {
	clientIDs := config.GetAllClientIDs()

	for _, clientID := range clientIDs {
		history, err := repository.GetSecretHistory(clientID)
		if err != nil {
			continue
		}

		var validHistory := make([]model.SecretVersion, 0)
		now := time.Now()

		for _, v := range history {
			if v.IsActive || v.ExpiresAt.After(now) {
				validHistory = append(validHistory, v)
			}
		}

		if len(validHistory) != len(history) {
			if err := repository.SetSecretHistory(clientID, validHistory); err != nil {
				return fmt.Errorf("failed to cleanup history for %s: %w", clientID, err)
			}
		}
	}

	return nil
}

func (s *SecretRotationService) generateNewSecret() string {
	return fmt.Sprintf("secret_%s_%s",
		util.RandomString(16),
		util.MD5Hash(fmt.Sprintf("%d", time.Now().UnixNano())[:16],
	)
}
