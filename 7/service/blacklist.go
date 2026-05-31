package service

import (
	"fmt"
	"time"

	"api-signature/repository"
)

type BlacklistService struct{}

func NewBlacklistService() *BlacklistService {
	return &BlacklistService{}
}

func (s *BlacklistService) AddIP(ip string, reason string, duration time.Duration) error {
	if ip == "" {
		return fmt.Errorf("ip address is required")
	}

	return repository.AddIPBlacklist(ip, reason, duration)
}

func (s *BlacklistService) RemoveIP(ip string) error {
	if ip == "" {
		return fmt.Errorf("ip address is required")
	}

	return repository.RemoveIPBlacklist(ip)
}

func (s *BlacklistService) IsIPBlocked(ip string) (bool, string, error) {
	if ip == "" {
		return false, "", nil
	}

	blocked, info, err := repository.IsIPBlacklisted(ip)
	if err != nil {
		return false, "", fmt.Errorf("failed to check IP blacklist: %w", err)
	}

	return blocked, info, nil
}

func (s *BlacklistService) AddIPToWhitelist(ip string) error {
	if ip == "" {
		return fmt.Errorf("ip address is required")
	}

	return repository.AddIPWhitelist(ip)
}

func (s *BlacklistService) RemoveIPFromWhitelist(ip string) error {
	if ip == "" {
		return fmt.Errorf("ip address is required")
	}

	return repository.RemoveIPWhitelist(ip)
}

func (s *BlacklistService) IsIPWhitelisted(ip string) (bool, error) {
	if ip == "" {
		return false, nil
	}

	return repository.IsIPWhitelisted(ip)
}

func (s *BlacklistService) CheckIPAccess(ip string) error {
	whitelisted, err := s.IsIPWhitelisted(ip)
	if err != nil {
		return err
	}

	if whitelisted {
		return nil
	}

	blocked, reason, err := s.IsIPBlocked(ip)
	if err != nil {
		return err
	}

	if blocked {
		return fmt.Errorf("ip is blocked: %s", reason)
	}

	return nil
}
