package service

import (
	"encoding/json"
	"fmt"
	"log"
	"time"

	"api-signature/model"
	"api-signature/repository"
	"api-signature/util"

	"github.com/google/uuid"
)

type AuditService struct{}

func NewAuditService() *AuditService {
	return &AuditService{}
}

func (s *AuditService) CreateAuditLog(
	clientID string,
	ip string,
	method string,
	path string,
	statusCode int,
	signature string,
	nonce string,
	requestTime int64,
	errMsg string,
) *model.AuditLog {
	return &model.AuditLog{
		ID:          uuid.New().String(),
		ClientID:    clientID,
		IP:          ip,
		Method:      method,
		Path:        path,
		StatusCode:  statusCode,
		Timestamp:   util.GetCurrentTimestamp(),
		Signature:   signature,
		Nonce:       nonce,
		RequestTime: requestTime,
		ErrorMsg:    errMsg,
	}
}

func (s *AuditService) SaveAuditLog(auditLog *model.AuditLog) error {
	if auditLog == nil {
		return fmt.Errorf("audit log is nil")
	}

	jsonData, err := json.Marshal(auditLog)
	if err != nil {
		return fmt.Errorf("failed to marshal audit log: %w", err)
	}

	if err := repository.SaveAuditLog(string(jsonData)); err != nil {
		log.Printf("Warning: Failed to save audit log to Redis: %v", err)
		return err
	}

	log.Printf("[AUDIT] %s | %s | %s %s | Status: %d | IP: %s | Time: %dms",
		auditLog.ID,
		auditLog.ClientID,
		auditLog.Method,
		auditLog.Path,
		auditLog.StatusCode,
		auditLog.IP,
		auditLog.RequestTime,
	)

	return nil
}

func (s *AuditService) LogRequest(
	clientID string,
	ip string,
	method string,
	path string,
	statusCode int,
	signature string,
	nonce string,
	startTime time.Time,
	errMsg string,
) {
	requestTime := time.Since(startTime).Milliseconds()

	auditLog := s.CreateAuditLog(
		clientID,
		ip,
		method,
		path,
		statusCode,
		signature,
		nonce,
		requestTime,
		errMsg,
	)

	if err := s.SaveAuditLog(auditLog); err != nil {
		log.Printf("Warning: Failed to save audit log: %v", err)
	}
}
