package service

import (
	"fmt"

	"api-signature/model"
)

type PermissionService struct{}

func NewPermissionService() *PermissionService {
	return &PermissionService{}
}

func (s *PermissionService) CheckPermission(client *model.ClientInfo, requiredPermission string) error {
	if client == nil {
		return fmt.Errorf("client is nil")
	}

	if requiredPermission == "" {
		return nil
	}

	requiredLevel := parsePermissionLevel(requiredPermission)
	if requiredLevel == model.PermissionNone {
		return nil
	}

	for _, perm := range client.Permissions {
		clientLevel := parsePermissionLevel(perm)
		if clientLevel >= requiredLevel {
			return nil
		}
	}

	return fmt.Errorf("permission denied: requires %s", requiredPermission)
}

func (s *PermissionService) HasPermission(client *model.ClientInfo, permission string) bool {
	if client == nil {
		return false
	}

	for _, perm := range client.Permissions {
		if perm == permission {
			return true
		}
	}

	return false
}

func (s *PermissionService) GetHighestPermission(client *model.ClientInfo) model.PermissionLevel {
	if client == nil {
		return model.PermissionNone
	}

	highest := model.PermissionNone
	for _, perm := range client.Permissions {
		level := parsePermissionLevel(perm)
		if level > highest {
			highest = level
		}
	}

	return highest
}

func (s *PermissionService) ValidatePermissions(client *model.ClientInfo, requiredPermissions []string) error {
	if client == nil {
		return fmt.Errorf("client is nil")
	}

	for _, perm := range requiredPermissions {
		if !s.HasPermission(client, perm) {
			return fmt.Errorf("missing required permission: %s", perm)
		}
	}

	return nil
}

func (s *PermissionService) GetAvailablePermissions() []string {
	return []string{
		model.PermissionRead.String(),
		model.PermissionWrite.String(),
		model.PermissionAdmin.String(),
	}
}

func parsePermissionLevel(permission string) model.PermissionLevel {
	switch permission {
	case "api:read":
		return model.PermissionRead
	case "api:write":
		return model.PermissionWrite
	case "api:admin":
		return model.PermissionAdmin
	default:
		return model.PermissionNone
	}
}
