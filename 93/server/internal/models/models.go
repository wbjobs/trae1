package models

import (
	"crypto/rand"
	"encoding/base64"
	"time"

	"github.com/go-webauthn/webauthn/protocol"
	"github.com/go-webauthn/webauthn/webauthn"
	"github.com/google/uuid"
)

type User struct {
	ID                string    `json:"id"`
	Username          string    `json:"username"`
	DisplayName       string    `json:"displayName"`
	Email             string    `json:"email,omitempty"`
	Phone             string    `json:"phone,omitempty"`
	CreatedAt         time.Time `json:"createdAt"`
	HasBackup         bool      `json:"hasBackup"`
}

type Credential struct {
	ID              string    `json:"id"`
	UserID          string    `json:"userId"`
	Name            string    `json:"name"`
	CreatedAt       time.Time `json:"createdAt"`
	LastUsedAt      time.Time `json:"lastUsedAt"`
	PublicKey       []byte    `json:"-"`
	AttestationType string    `json:"attestationType"`
	AAGUID          []byte    `json:"aaguid"`
	SignCount       uint32    `json:"signCount"`
	BackupEligible  bool      `json:"backupEligible"`
	BackupState     bool      `json:"backupState"`
	Transports      []string  `json:"transports,omitempty"`
}

type SessionData struct {
	Challenge        string                               `json:"challenge"`
	UserID           string                               `json:"userId"`
	UserVerification protocol.UserVerificationRequirement `json:"userVerification"`
	ExpiresAt        time.Time                            `json:"expiresAt"`
}

type EncryptedBackup struct {
	ID         string    `json:"id"`
	UserID     string    `json:"userId"`
	Salt       string    `json:"salt"`
	Nonce      string    `json:"nonce"`
	Ciphertext string    `json:"ciphertext"`
	CreatedAt  time.Time `json:"createdAt"`
	UpdatedAt  time.Time `json:"updatedAt"`
}

type RecoveryCode struct {
	ID        string    `json:"id"`
	UserID    string    `json:"userId"`
	CodeHash  string    `json:"codeHash"`
	Used      bool      `json:"used"`
	CreatedAt time.Time `json:"createdAt"`
	ExpiresAt time.Time `json:"expiresAt"`
}

type VerificationSession struct {
	ID         string    `json:"id"`
	UserID     string    `json:"userId"`
	Type       string    `json:"type"`
	CodeHash   string    `json:"codeHash"`
	Attempts   int       `json:"attempts"`
	Verified   bool      `json:"verified"`
	CreatedAt  time.Time `json:"createdAt"`
	ExpiresAt  time.Time `json:"expiresAt"`
}

type RecoverySession struct {
	ID         string    `json:"id"`
	UserID     string    `json:"userId"`
	Method     string    `json:"method"`
	Challenge  string    `json:"challenge"`
	ExpiresAt  time.Time `json:"expiresAt"`
}

func (u *User) WebAuthnID() []byte {
	return []byte(u.ID)
}

func (u *User) WebAuthnName() string {
	return u.Username
}

func (u *User) WebAuthnDisplayName() string {
	return u.DisplayName
}

func (u *User) WebAuthnCredentials() []webauthn.Credential {
	return nil
}

func (u *User) WebAuthnIcon() string {
	return ""
}

func NewUser(username, displayName string) *User {
	return &User{
		ID:          uuid.New().String(),
		Username:    username,
		DisplayName: displayName,
		CreatedAt:   time.Now(),
	}
}

func GenerateChallenge() (string, error) {
	challenge := make([]byte, 32)
	_, err := rand.Read(challenge)
	if err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(challenge), nil
}
