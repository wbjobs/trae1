package store

import (
	"errors"
	"sync"
	"time"

	"github.com/google/uuid"
	"webauthn-auth/internal/models"
)

type Store struct {
	mu                  sync.RWMutex
	users               map[string]*models.User
	credentials         map[string][]*models.Credential
	sessions            map[string]*models.SessionData
	backups             map[string]*models.EncryptedBackup
	recoveryCodes       map[string][]*models.RecoveryCode
	verificationSessions map[string]*models.VerificationSession
	recoverySessions    map[string]*models.RecoverySession
}

func New() *Store {
	return &Store{
		users:               make(map[string]*models.User),
		credentials:         make(map[string][]*models.Credential),
		sessions:            make(map[string]*models.SessionData),
		backups:             make(map[string]*models.EncryptedBackup),
		recoveryCodes:       make(map[string][]*models.RecoveryCode),
		verificationSessions: make(map[string]*models.VerificationSession),
		recoverySessions:    make(map[string]*models.RecoverySession),
	}
}

func (s *Store) CreateUser(user *models.User) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, exists := s.users[user.Username]; exists {
		return errors.New("user already exists")
	}
	s.users[user.Username] = user
	s.credentials[user.Username] = []*models.Credential{}
	return nil
}

func (s *Store) GetUser(username string) (*models.User, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	user, exists := s.users[username]
	if !exists {
		return nil, errors.New("user not found")
	}
	return user, nil
}

func (s *Store) AddCredential(username string, credential *models.Credential) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.credentials[username] == nil {
		return errors.New("user not found")
	}
	if len(s.credentials[username]) >= 5 {
		return errors.New("maximum 5 devices allowed")
	}
	s.credentials[username] = append(s.credentials[username], credential)
	return nil
}

func (s *Store) GetCredentials(username string) ([]*models.Credential, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	creds, exists := s.credentials[username]
	if !exists {
		return nil, errors.New("user not found")
	}
	return creds, nil
}

func (s *Store) DeleteCredential(username, credentialID string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	creds, exists := s.credentials[username]
	if !exists {
		return errors.New("user not found")
	}
	for i, cred := range creds {
		if cred.ID == credentialID {
			s.credentials[username] = append(creds[:i], creds[i+1:]...)
			return nil
		}
	}
	return errors.New("credential not found")
}

func (s *Store) UpdateCredentialSignCount(username, credentialID string, signCount uint32) {
	s.mu.Lock()
	defer s.mu.Unlock()
	creds, exists := s.credentials[username]
	if !exists {
		return
	}
	for _, cred := range creds {
		if cred.ID == credentialID {
			cred.SignCount = signCount
			cred.LastUsedAt = time.Now()
			return
		}
	}
}

func (s *Store) SaveSession(session *models.SessionData) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.sessions[session.Challenge] = session
}

func (s *Store) GetSession(challenge string) (*models.SessionData, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	session, exists := s.sessions[challenge]
	if !exists {
		return nil, errors.New("session not found")
	}
	if time.Now().After(session.ExpiresAt) {
		delete(s.sessions, challenge)
		return nil, errors.New("session expired")
	}
	return session, nil
}

func (s *Store) DeleteSession(challenge string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.sessions, challenge)
}

func (s *Store) SaveBackup(backup *models.EncryptedBackup) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.backups[backup.UserID] = backup
	if user, exists := s.users[backup.UserID]; exists {
		user.HasBackup = true
	}
}

func (s *Store) GetBackup(userID string) (*models.EncryptedBackup, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	backup, exists := s.backups[userID]
	if !exists {
		return nil, errors.New("backup not found")
	}
	return backup, nil
}

func (s *Store) DeleteBackup(userID string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.backups, userID)
	if user, exists := s.users[userID]; exists {
		user.HasBackup = false
	}
}

func (s *Store) AddRecoveryCode(userID string, codeHash string) *models.RecoveryCode {
	s.mu.Lock()
	defer s.mu.Unlock()
	code := &models.RecoveryCode{
		ID:        uuid.New().String(),
		UserID:    userID,
		CodeHash:  codeHash,
		Used:      false,
		CreatedAt: time.Now(),
		ExpiresAt: time.Now().Add(30 * 24 * time.Hour),
	}
	if s.recoveryCodes[userID] == nil {
		s.recoveryCodes[userID] = []*models.RecoveryCode{}
	}
	s.recoveryCodes[userID] = append(s.recoveryCodes[userID], code)
	return code
}

func (s *Store) ValidateRecoveryCode(userID, codeHash string) (*models.RecoveryCode, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	codes, exists := s.recoveryCodes[userID]
	if !exists {
		return nil, errors.New("no recovery codes found")
	}
	for _, code := range codes {
		if code.CodeHash == codeHash && !code.Used && time.Now().Before(code.ExpiresAt) {
			code.Used = true
			return code, nil
		}
	}
	return nil, errors.New("invalid or expired recovery code")
}

func (s *Store) CreateVerificationSession(userID, verifyType, codeHash string) *models.VerificationSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	session := &models.VerificationSession{
		ID:        uuid.New().String(),
		UserID:    userID,
		Type:      verifyType,
		CodeHash:  codeHash,
		Attempts:  0,
		Verified:  false,
		CreatedAt: time.Now(),
		ExpiresAt: time.Now().Add(10 * time.Minute),
	}
	s.verificationSessions[session.ID] = session
	return session
}

func (s *Store) GetVerificationSession(sessionID string) (*models.VerificationSession, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	session, exists := s.verificationSessions[sessionID]
	if !exists {
		return nil, errors.New("verification session not found")
	}
	if time.Now().After(session.ExpiresAt) {
		delete(s.verificationSessions, sessionID)
		return nil, errors.New("verification session expired")
	}
	return session, nil
}

func (s *Store) VerifyCode(sessionID, codeHash string) (bool, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	session, exists := s.verificationSessions[sessionID]
	if !exists {
		return false, errors.New("verification session not found")
	}
	session.Attempts++
	if session.Attempts > 5 {
		delete(s.verificationSessions, sessionID)
		return false, errors.New("too many attempts")
	}
	if session.CodeHash == codeHash {
		session.Verified = true
		return true, nil
	}
	return false, nil
}

func (s *Store) CreateRecoverySession(userID, method string) *models.RecoverySession {
	s.mu.Lock()
	defer s.mu.Unlock()
	challenge := make([]byte, 32)
	uuid.New().MarshalBinary()
	session := &models.RecoverySession{
		ID:        uuid.New().String(),
		UserID:    userID,
		Method:    method,
		Challenge: uuid.New().String(),
		ExpiresAt: time.Now().Add(15 * time.Minute),
	}
	s.recoverySessions[session.ID] = session
	return session
}

func (s *Store) GetRecoverySession(sessionID string) (*models.RecoverySession, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	session, exists := s.recoverySessions[sessionID]
	if !exists {
		return nil, errors.New("recovery session not found")
	}
	if time.Now().After(session.ExpiresAt) {
		delete(s.recoverySessions, sessionID)
		return nil, errors.New("recovery session expired")
	}
	return session, nil
}

func (s *Store) DeleteRecoverySession(sessionID string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.recoverySessions, sessionID)
}

func (s *Store) UpdateUserContact(username, email, phone string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	user, exists := s.users[username]
	if !exists {
		return errors.New("user not found")
	}
	if email != "" {
		user.Email = email
	}
	if phone != "" {
		user.Phone = phone
	}
	return nil
}
