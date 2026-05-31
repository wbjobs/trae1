package handlers

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/go-webauthn/webauthn/protocol"
	"github.com/go-webauthn/webauthn/webauthn"
	"github.com/google/uuid"
	"golang.org/x/crypto/bcrypt"

	"webauthn-auth/internal/crypto"
	"webauthn-auth/internal/middleware"
	"webauthn-auth/internal/models"
	"webauthn-auth/internal/store"
)

type RecoveryHandler struct {
	store    *store.Store
	webauthn *webauthn.WebAuthn
}

func NewRecoveryHandler(s *store.Store, wa *webauthn.WebAuthn) *RecoveryHandler {
	return &RecoveryHandler{
		store:    s,
		webauthn: wa,
	}
}

type BackupCredentialsRequest struct {
	Password string `json:"password"`
}

type BackupCredentialsResponse struct {
	Status        string `json:"status"`
	RecoveryCode  string `json:"recoveryCode"`
	BackupCreated bool   `json:"backupCreated"`
}

func (h *RecoveryHandler) BackupCredentials(w http.ResponseWriter, r *http.Request) {
	username, ok := middleware.GetUsernameFromContext(r.Context())
	if !ok {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}

	var req BackupCredentialsRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	if len(req.Password) < 8 {
		http.Error(w, "password must be at least 8 characters", http.StatusBadRequest)
		return
	}

	creds, err := h.store.GetCredentials(username)
	if err != nil {
		http.Error(w, "failed to get credentials", http.StatusInternalServerError)
		return
	}

	if len(creds) == 0 {
		http.Error(w, "no credentials to backup", http.StatusBadRequest)
		return
	}

	type credentialBackup struct {
		ID              string `json:"id"`
		PublicKey       string `json:"publicKey"`
		Name            string `json:"name"`
		AttestationType string `json:"attestationType"`
		AAGUID          string `json:"aaguid"`
		SignCount       uint32 `json:"signCount"`
		BackupEligible  bool   `json:"backupEligible"`
	}

	var backupList []credentialBackup
	for _, cred := range creds {
		backupList = append(backupList, credentialBackup{
			ID:              cred.ID,
			PublicKey:       base64.StdEncoding.EncodeToString(cred.PublicKey),
			Name:            cred.Name,
			AttestationType: cred.AttestationType,
			AAGUID:          base64.StdEncoding.EncodeToString(cred.AAGUID),
			SignCount:       cred.SignCount,
			BackupEligible:  cred.BackupEligible,
		})
	}

	plaintext, err := json.Marshal(map[string]interface{}{
		"username":    username,
		"credentials": backupList,
		"backupTime":  time.Now(),
		"version":     "1.0",
	})
	if err != nil {
		http.Error(w, "failed to marshal backup data", http.StatusInternalServerError)
		return
	}

	encrypted, err := crypto.Encrypt(plaintext, req.Password)
	if err != nil {
		http.Error(w, "encryption failed", http.StatusInternalServerError)
		return
	}

	backup := &models.EncryptedBackup{
		ID:         uuid.New().String(),
		UserID:     username,
		Salt:       encrypted.Salt,
		Nonce:      encrypted.Nonce,
		Ciphertext: encrypted.Ciphertext,
		CreatedAt:  time.Now(),
		UpdatedAt:  time.Now(),
	}

	h.store.SaveBackup(backup)

	recoveryCode := crypto.GenerateRecoveryCode()
	codeHash := crypto.HashCode(recoveryCode)
	h.store.AddRecoveryCode(username, codeHash)

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(BackupCredentialsResponse{
		Status:        "success",
		RecoveryCode:  recoveryCode,
		BackupCreated: true,
	})
}

type DecryptBackupRequest struct {
	Password string `json:"password"`
}

type CredentialInfo struct {
	ID              string    `json:"id"`
	Name            string    `json:"name"`
	AttestationType string    `json:"attestationType"`
	SignCount       uint32    `json:"signCount"`
	BackupEligible  bool      `json:"backupEligible"`
	RegisteredAt    time.Time `json:"registeredAt"`
}

type DecryptBackupResponse struct {
	Status       string           `json:"status"`
	Credentials  []CredentialInfo `json:"credentials"`
	BackupTime   time.Time        `json:"backupTime"`
}

func (h *RecoveryHandler) DecryptBackup(w http.ResponseWriter, r *http.Request) {
	username, ok := middleware.GetUsernameFromContext(r.Context())
	if !ok {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}

	var req DecryptBackupRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	backup, err := h.store.GetBackup(username)
	if err != nil {
		http.Error(w, "no backup found", http.StatusNotFound)
		return
	}

	encrypted := &crypto.EncryptedData{
		Salt:       backup.Salt,
		Nonce:      backup.Nonce,
		Ciphertext: backup.Ciphertext,
	}

	plaintext, err := crypto.Decrypt(encrypted, req.Password)
	if err != nil {
		http.Error(w, "decryption failed - invalid password", http.StatusUnauthorized)
		return
	}

	var backupData struct {
		Credentials []struct {
			ID              string `json:"id"`
			PublicKey       string `json:"publicKey"`
			Name            string `json:"name"`
			AttestationType string `json:"attestationType"`
			AAGUID          string `json:"aaguid"`
			SignCount       uint32 `json:"signCount"`
			BackupEligible  bool   `json:"backupEligible"`
		} `json:"credentials"`
		BackupTime time.Time `json:"backupTime"`
	}

	if err := json.Unmarshal(plaintext, &backupData); err != nil {
		http.Error(w, "invalid backup data", http.StatusInternalServerError)
		return
	}

	var creds []CredentialInfo
	for _, cred := range backupData.Credentials {
		creds = append(creds, CredentialInfo{
			ID:              cred.ID,
			Name:            cred.Name,
			AttestationType: cred.AttestationType,
			SignCount:       cred.SignCount,
			BackupEligible:  cred.BackupEligible,
			RegisteredAt:    backup.BackupTime,
		})
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(DecryptBackupResponse{
		Status:      "success",
		Credentials: creds,
		BackupTime:  backupData.BackupTime,
	})
}

type SendCodeRequest struct {
	Username string `json:"username"`
	Type     string `json:"type"`
	Target   string `json:"target"`
}

type SendCodeResponse struct {
	Status    string `json:"status"`
	SessionID string `json:"sessionId"`
	Message   string `json:"message"`
}

func (h *RecoveryHandler) SendVerificationCode(w http.ResponseWriter, r *http.Request) {
	var req SendCodeRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	if req.Username == "" {
		http.Error(w, "username is required", http.StatusBadRequest)
		return
	}

	user, err := h.store.GetUser(req.Username)
	if err != nil {
		http.Error(w, "user not found", http.StatusNotFound)
		return
	}

	_, err = h.store.GetBackup(req.Username)
	if err != nil {
		http.Error(w, "no backup found for this user", http.StatusNotFound)
		return
	}

	code := crypto.GenerateVerificationCode()
	codeHash := crypto.HashCode(code)

	var target string
	switch req.Type {
	case "email":
		target = req.Target
		if target == "" {
			target = user.Email
		}
		if target == "" {
			http.Error(w, "no email registered", http.StatusBadRequest)
			return
		}
		fmt.Printf("📧 [MOCK] Sending verification code to email %s: %s\n", target, code)
	case "sms":
		target = req.Target
		if target == "" {
			target = user.Phone
		}
		if target == "" {
			http.Error(w, "no phone registered", http.StatusBadRequest)
			return
		}
		fmt.Printf("📱 [MOCK] Sending verification code to SMS %s: %s\n", target, code)
	default:
		http.Error(w, "invalid verification type, use 'email' or 'sms'", http.StatusBadRequest)
		return
	}

	session := h.store.CreateVerificationSession(req.Username, req.Type, codeHash)

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(SendCodeResponse{
		Status:    "success",
		SessionID: session.ID,
		Message:   fmt.Sprintf("verification code sent to %s", req.Type),
	})
}

type VerifyCodeRequest struct {
	SessionID string `json:"sessionId"`
	Code      string `json:"code"`
}

type VerifyCodeResponse struct {
	Status          string `json:"status"`
	RecoveryToken   string `json:"recoveryToken"`
	RecoveryTokenID string `json:"recoveryTokenId"`
}

func (h *RecoveryHandler) VerifyCode(w http.ResponseWriter, r *http.Request) {
	var req VerifyCodeRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	if req.SessionID == "" || req.Code == "" {
		http.Error(w, "sessionId and code are required", http.StatusBadRequest)
		return
	}

	codeHash := crypto.HashCode(strings.TrimSpace(req.Code))
	verified, err := h.store.VerifyCode(req.SessionID, codeHash)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	if !verified {
		http.Error(w, "invalid verification code", http.StatusUnauthorized)
		return
	}

	session, err := h.store.GetVerificationSession(req.SessionID)
	if err != nil {
		http.Error(w, "session not found", http.StatusNotFound)
		return
	}

	recoverySession := h.store.CreateRecoverySession(session.UserID, session.Type)

	token, err := middleware.GenerateRecoveryToken(session.UserID, recoverySession.ID)
	if err != nil {
		http.Error(w, "failed to generate recovery token", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(VerifyCodeResponse{
		Status:          "success",
		RecoveryToken:   token,
		RecoveryTokenID: recoverySession.ID,
	})
}

type VerifyRecoveryCodeRequest struct {
	Username     string `json:"username"`
	RecoveryCode string `json:"recoveryCode"`
}

func (h *RecoveryHandler) VerifyRecoveryCode(w http.ResponseWriter, r *http.Request) {
	var req VerifyRecoveryCodeRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	if req.Username == "" || req.RecoveryCode == "" {
		http.Error(w, "username and recoveryCode are required", http.StatusBadRequest)
		return
	}

	_, err := h.store.GetBackup(req.Username)
	if err != nil {
		http.Error(w, "no backup found for this user", http.StatusNotFound)
		return
	}

	codeHash := crypto.HashCode(strings.ToUpper(strings.TrimSpace(req.RecoveryCode)))
	_, err = h.store.ValidateRecoveryCode(req.Username, codeHash)
	if err != nil {
		http.Error(w, err.Error(), http.StatusUnauthorized)
		return
	}

	recoverySession := h.store.CreateRecoverySession(req.Username, "recovery_code")
	token, err := middleware.GenerateRecoveryToken(req.Username, recoverySession.ID)
	if err != nil {
		http.Error(w, "failed to generate recovery token", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(VerifyCodeResponse{
		Status:          "success",
		RecoveryToken:   token,
		RecoveryTokenID: recoverySession.ID,
	})
}

type BeginRecoveryRequest struct {
	RecoveryTokenID string `json:"recoveryTokenId"`
	Password        string `json:"password"`
	DeviceName      string `json:"deviceName"`
}

type BeginRecoveryResponse struct {
	Challenge      string      `json:"challenge"`
	RelyingParty   interface{} `json:"rp"`
	User           interface{} `json:"user"`
	PubKeyCredParams []interface{} `json:"pubKeyCredParams"`
	Timeout        int         `json:"timeout"`
	AuthenticatorSelection interface{} `json:"authenticatorSelection"`
	Attestation    string      `json:"attestation"`
	SessionID      string      `json:"sessionId"`
}

func (h *RecoveryHandler) BeginRecovery(w http.ResponseWriter, r *http.Request) {
	tokenUsername, ok := middleware.GetRecoveryUsernameFromContext(r.Context())
	if !ok {
		http.Error(w, "unauthorized - recovery token required", http.StatusUnauthorized)
		return
	}

	var req BeginRecoveryRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	recoverySession, err := h.store.GetRecoverySession(req.RecoveryTokenID)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	if recoverySession.UserID != tokenUsername {
		http.Error(w, "recovery token mismatch", http.StatusUnauthorized)
		return
	}

	backup, err := h.store.GetBackup(tokenUsername)
	if err != nil {
		http.Error(w, "no backup found", http.StatusNotFound)
		return
	}

	encrypted := &crypto.EncryptedData{
		Salt:       backup.Salt,
		Nonce:      backup.Nonce,
		Ciphertext: backup.Ciphertext,
	}

	plaintext, err := crypto.Decrypt(encrypted, req.Password)
	if err != nil {
		http.Error(w, "invalid backup password", http.StatusUnauthorized)
		return
	}

	var backupData struct {
		Credentials []struct {
			ID              string `json:"id"`
			PublicKey       string `json:"publicKey"`
			Name            string `json:"name"`
			AttestationType string `json:"attestationType"`
			AAGUID          string `json:"aaguid"`
			SignCount       uint32 `json:"signCount"`
			BackupEligible  bool   `json:"backupEligible"`
		} `json:"credentials"`
	}
	json.Unmarshal(plaintext, &backupData)

	user, err := h.store.GetUser(tokenUsername)
	if err != nil {
		http.Error(w, "user not found", http.StatusNotFound)
		return
	}

	options, sessionData, err := h.webauthn.BeginRegistration(user)
	if err != nil {
		http.Error(w, "failed to begin registration", http.StatusInternalServerError)
		return
	}

	challengeBytes, _ := protocol.URLEncodedBase64.Raw().DecodeToString(options.Response.Challenge)
	session := &models.SessionData{
		Challenge:        challengeBytes,
		UserID:           user.Username,
		UserVerification: options.Response.AuthenticatorSelection.UserVerification,
		ExpiresAt:        time.Now().Add(5 * time.Minute),
	}
	h.store.SaveSession(session)

	recoverySession.Challenge = challengeBytes
	recoverySession.ExpiresAt = time.Now().Add(5 * time.Minute)

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":    "success",
		"sessionId": sessionData.Challenge,
		"options":   options,
		"backupCredCount": len(backupData.Credentials),
	})
}

type FinishRecoveryRequest struct {
	Challenge       string                 `json:"challenge"`
	RecoveryTokenID string                 `json:"recoveryTokenId"`
	DeviceName      string                 `json:"deviceName"`
	Response        map[string]interface{} `json:"response"`
}

func (h *RecoveryHandler) FinishRecovery(w http.ResponseWriter, r *http.Request) {
	tokenUsername, ok := middleware.GetRecoveryUsernameFromContext(r.Context())
	if !ok {
		http.Error(w, "unauthorized - recovery token required", http.StatusUnauthorized)
		return
	}

	challenge := r.URL.Query().Get("challenge")
	recoveryTokenID := r.URL.Query().Get("recoveryTokenId")
	deviceName := r.URL.Query().Get("deviceName")

	session, err := h.store.GetSession(challenge)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	recoverySession, err := h.store.GetRecoverySession(recoveryTokenID)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	if recoverySession.UserID != tokenUsername || session.UserID != tokenUsername {
		http.Error(w, "user mismatch", http.StatusUnauthorized)
		return
	}

	user, err := h.store.GetUser(tokenUsername)
	if err != nil {
		http.Error(w, "user not found", http.StatusNotFound)
		return
	}

	parsedResponse, err := protocol.ParseCredentialCreationResponse(r)
	if err != nil {
		http.Error(w, "failed to parse response: "+err.Error(), http.StatusBadRequest)
		return
	}

	webauthnSession := &webauthn.SessionData{
		Challenge:        session.Challenge,
		UserID:           []byte(user.ID),
		UserVerification: session.UserVerification,
	}

	credential, err := h.webauthn.CreateCredential(user, *webauthnSession, parsedResponse)
	if err != nil {
		http.Error(w, "failed to create credential: "+err.Error(), http.StatusInternalServerError)
		return
	}

	credID := base64.RawURLEncoding.EncodeToString(credential.ID)
	credentialModel := &models.Credential{
		ID:              credID,
		UserID:          user.Username,
		Name:            deviceName,
		CreatedAt:       time.Now(),
		LastUsedAt:      time.Now(),
		PublicKey:       credential.PublicKey,
		AttestationType: credential.AttestationType,
		AAGUID:          credential.AAGUID,
		SignCount:       credential.SignCount,
		BackupEligible:  credential.Flags.BackupEligible,
		BackupState:     credential.Flags.BackupState,
		Transports:      credential.Transport,
	}

	if err := h.store.AddCredential(user.Username, credentialModel); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	h.store.DeleteSession(challenge)
	h.store.DeleteRecoverySession(recoveryTokenID)

	token, err := middleware.GenerateToken(user.Username)
	if err != nil {
		http.Error(w, "failed to generate token", http.StatusInternalServerError)
		return
	}

	creds, _ := h.store.GetCredentials(user.Username)

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":  "success",
		"message": "device recovered and bound successfully",
		"token":   token,
		"user":    user,
		"deviceCount": len(creds),
		"newDevice": credentialModel,
	})
}

type OIDCLoginRequest struct {
	Provider string `json:"provider"`
	Token    string `json:"token"`
}

type OIDCConfig struct {
	ClientID     string
	ClientSecret string
	IssuerURL    string
}

func (h *RecoveryHandler) OIDCLogin(w http.ResponseWriter, r *http.Request) {
	var req OIDCLoginRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	userInfo, err := h.verifyOIDCToken(req.Provider, req.Token)
	if err != nil {
		http.Error(w, "oidc verification failed: "+err.Error(), http.StatusUnauthorized)
		return
	}

	user, err := h.store.GetUser(userInfo.Email)
	if err != nil {
		http.Error(w, "user not found, please register first", http.StatusNotFound)
		return
	}

	_, err = h.store.GetBackup(user.Username)
	if err != nil {
		http.Error(w, "no backup found for this user", http.StatusNotFound)
		return
	}

	recoverySession := h.store.CreateRecoverySession(user.Username, "oidc_"+req.Provider)
	token, err := middleware.GenerateRecoveryToken(user.Username, recoverySession.ID)
	if err != nil {
		http.Error(w, "failed to generate recovery token", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":          "success",
		"recoveryToken":   token,
		"recoveryTokenId": recoverySession.ID,
		"user":            user,
	})
}

type OIDCUserInfo struct {
	Subject string `json:"sub"`
	Email   string `json:"email"`
	Name    string `json:"name"`
}

func (h *RecoveryHandler) verifyOIDCToken(provider, token string) (*OIDCUserInfo, error) {
	userInfo := &OIDCUserInfo{
		Subject: "mock-subject-12345",
		Email:   "user@example.com",
		Name:    "Mock User",
	}

	if token == "" {
		return nil, errors.New("invalid token")
	}

	fmt.Printf("🔐 [MOCK] OIDC verification for provider: %s, token: %s...\n", provider, token[:min(20, len(token))])

	return userInfo, nil
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func HashPassword(password string) (string, error) {
	bytes, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
	return string(bytes), err
}

func CheckPasswordHash(password, hash string) bool {
	err := bcrypt.CompareHashAndPassword([]byte(hash), []byte(password))
	return err == nil
}
