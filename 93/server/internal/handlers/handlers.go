package handlers

import (
	"encoding/base64"
	"encoding/json"
	"log"
	"net/http"
	"os"
	"time"

	"github.com/go-webauthn/webauthn/protocol"
	"github.com/go-webauthn/webauthn/webauthn"
	"github.com/gorilla/mux"

	"webauthn-auth/internal/middleware"
	"webauthn-auth/internal/models"
	"webauthn-auth/internal/risk"
	"webauthn-auth/internal/store"
)

type Handler struct {
	store      *store.Store
	webauthn   *webauthn.WebAuthn
	riskEngine *risk.RiskEngine
}

func New(s *store.Store) (*Handler, error) {
	rpID := os.Getenv("RP_ID")
	rpOrigin := os.Getenv("RP_ORIGIN")
	rpDisplayName := os.Getenv("RP_DISPLAY_NAME")

	if rpID == "" {
		rpID = "localhost"
	}
	if rpOrigin == "" {
		rpOrigin = "http://localhost:3000"
	}
	if rpDisplayName == "" {
		rpDisplayName = "WebAuthn Demo"
	}

	config := &webauthn.Config{
		RPDisplayName: rpDisplayName,
		RPID:          rpID,
		RPOrigin:      rpOrigin,
		RPOrigins:     []string{rpOrigin},
	}

	wa, err := webauthn.New(config)
	if err != nil {
		return nil, err
	}

	return &Handler{store: s, webauthn: wa}, nil
}

func (h *Handler) SetRiskEngine(engine *risk.RiskEngine) {
	h.riskEngine = engine
}

func (h *Handler) GetWebAuthn() *webauthn.WebAuthn {
	return h.webauthn
}

type RegisterRequest struct {
	Username    string `json:"username"`
	DisplayName string `json:"displayName"`
	DeviceName  string `json:"deviceName"`
}

type RegisterBeginResponse struct {
	Challenge    string      `json:"challenge"`
	RelyingParty interface{} `json:"rp"`
	User         interface{} `json:"user"`
	PubKeyCredParams []interface{} `json:"pubKeyCredParams"`
	Timeout      int         `json:"timeout"`
	ExcludeCredentials []interface{} `json:"excludeCredentials"`
	AuthenticatorSelection interface{} `json:"authenticatorSelection"`
	Attestation  string      `json:"attestation"`
}

func (h *Handler) BeginRegister(w http.ResponseWriter, r *http.Request) {
	var req RegisterRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	user := models.NewUser(req.Username, req.DisplayName)
	if err := h.store.CreateUser(user); err != nil {
		http.Error(w, err.Error(), http.StatusConflict)
		return
	}

	options, sessionData, err := h.webauthn.BeginRegistration(user)
	if err != nil {
		http.Error(w, "failed to begin registration", http.StatusInternalServerError)
		return
	}

	challengeBytes, err := protocol.URLEncodedBase64.Raw().DecodeToString(options.Response.Challenge)
	if err != nil {
		http.Error(w, "failed to decode challenge", http.StatusInternalServerError)
		return
	}

	session := &models.SessionData{
		Challenge:        challengeBytes,
		UserID:           user.Username,
		UserVerification: string(options.Response.AuthenticatorSelection.UserVerification),
		ExpiresAt:        time.Now().Add(5 * time.Minute),
	}
	h.store.SaveSession(session)

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(options)
}

func (h *Handler) FinishRegister(w http.ResponseWriter, r *http.Request) {
	challenge := r.URL.Query().Get("challenge")
	session, err := h.store.GetSession(challenge)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	user, err := h.store.GetUser(session.UserID)
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
		UserVerification: protocol.UserVerificationRequirement(session.UserVerification),
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
		Name:            r.URL.Query().Get("deviceName"),
		CreatedAt:       time.Now(),
		LastUsedAt:      time.Now(),
		PublicKey:       credential.PublicKey,
		AttestationType: credential.AttestationType,
		AAGUID:          credential.AAGUID,
		SignCount:       credential.SignCount,
		BackupEligible:  credential.Flags.BackupEligible,
		BackupState:     credential.Flags.BackupState,
	}

	if err := h.store.AddCredential(user.Username, credentialModel); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	h.store.DeleteSession(challenge)

	token, err := middleware.GenerateToken(user.Username)
	if err != nil {
		http.Error(w, "failed to generate token", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status": "success",
		"token":  token,
		"user":   user,
	})
}

type LoginRequest struct {
	Username string `json:"username"`
}

func (h *Handler) BeginLogin(w http.ResponseWriter, r *http.Request) {
	var req LoginRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	user, err := h.store.GetUser(req.Username)
	if err != nil {
		http.Error(w, "user not found", http.StatusNotFound)
		return
	}

	creds, err := h.store.GetCredentials(req.Username)
	if err != nil || len(creds) == 0 {
		http.Error(w, "no credentials found", http.StatusBadRequest)
		return
	}

	var riskAssessment *risk.RiskAssessment
	if h.riskEngine != nil {
		riskAssessment = h.riskEngine.AssessRisk(r, req.Username)
		log.Printf("[Risk] User: %s, Score: %.1f, Level: %s, Action: %s",
			req.Username, riskAssessment.Score, riskAssessment.LevelName, riskAssessment.Action.Type)
	}

	var webauthnCreds []webauthn.Credential
	for _, c := range creds {
		idBytes, _ := base64.RawURLEncoding.DecodeString(c.ID)
		webauthnCreds = append(webauthnCreds, webauthn.Credential{
			ID:              idBytes,
			PublicKey:       c.PublicKey,
			AttestationType: c.AttestationType,
			AAGUID:          c.AAGUID,
			SignCount:       c.SignCount,
		})
	}

	webauthnUser := &webauthnUserWrapper{
		user:        user,
		credentials: webauthnCreds,
	}

	options, sessionData, err := h.webauthn.BeginLogin(webauthnUser)
	if err != nil {
		http.Error(w, "failed to begin login", http.StatusInternalServerError)
		return
	}

	challengeBytes, _ := protocol.URLEncodedBase64.Raw().DecodeToString(options.Response.Challenge)
	session := &models.SessionData{
		Challenge:        challengeBytes,
		UserID:           user.Username,
		UserVerification: protocol.UserVerificationRequirement(options.Response.UserVerification),
		ExpiresAt:        time.Now().Add(5 * time.Minute),
	}
	h.store.SaveSession(session)

	response := map[string]interface{}{
		"options": options,
	}

	if riskAssessment != nil {
		response["risk"] = map[string]interface{}{
			"score":   riskAssessment.Score,
			"level":   riskAssessment.LevelName,
			"action":  riskAssessment.Action,
			"factors": riskAssessment.Factors,
		}

		if riskAssessment.Level >= risk.RiskHigh {
			response["requiresAdditionalAuth"] = true
			response["additionalAuthMethods"] = riskAssessment.Action.Methods
		}
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (h *Handler) FinishLogin(w http.ResponseWriter, r *http.Request) {
	challenge := r.URL.Query().Get("challenge")
	session, err := h.store.GetSession(challenge)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	user, err := h.store.GetUser(session.UserID)
	if err != nil {
		http.Error(w, "user not found", http.StatusNotFound)
		return
	}

	creds, err := h.store.GetCredentials(session.UserID)
	if err != nil {
		http.Error(w, "no credentials found", http.StatusBadRequest)
		return
	}

	var webauthnCreds []webauthn.Credential
	for _, c := range creds {
		idBytes, _ := base64.RawURLEncoding.DecodeString(c.ID)
		webauthnCreds = append(webauthnCreds, webauthn.Credential{
			ID:              idBytes,
			PublicKey:       c.PublicKey,
			AttestationType: c.AttestationType,
			AAGUID:          c.AAGUID,
			SignCount:       c.SignCount,
		})
	}

	webauthnUser := &webauthnUserWrapper{
		user:        user,
		credentials: webauthnCreds,
	}

	webauthnSession := &webauthn.SessionData{
		Challenge:        session.Challenge,
		UserID:           []byte(user.ID),
		UserVerification: protocol.UserVerificationRequirement(session.UserVerification),
	}

	parsedResponse, err := protocol.ParseCredentialRequestResponse(r)
	if err != nil {
		http.Error(w, "failed to parse response: "+err.Error(), http.StatusBadRequest)
		return
	}

	credential, err := h.webauthn.ValidateLogin(webauthnUser, *webauthnSession, parsedResponse)
	if err != nil {
		http.Error(w, "login validation failed: "+err.Error(), http.StatusUnauthorized)
		return
	}

	credID := base64.RawURLEncoding.EncodeToString(credential.ID)
	h.store.UpdateCredentialSignCount(user.Username, credID, credential.SignCount)

	h.store.DeleteSession(challenge)

	if h.riskEngine != nil {
		ctx := risk.CollectContext(r, user.Username, nil)
		h.riskEngine.RecordLogin(user.Username, ctx, true)
	}

	token, err := middleware.GenerateToken(user.Username)
	if err != nil {
		http.Error(w, "failed to generate token", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status": "success",
		"token":  token,
		"user":   user,
	})
}

func (h *Handler) GetCredentials(w http.ResponseWriter, r *http.Request) {
	username, ok := middleware.GetUsernameFromContext(r.Context())
	if !ok {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}

	creds, err := h.store.GetCredentials(username)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"credentials": creds,
		"count":       len(creds),
		"maxDevices":  5,
	})
}

func (h *Handler) DeleteCredential(w http.ResponseWriter, r *http.Request) {
	username, ok := middleware.GetUsernameFromContext(r.Context())
	if !ok {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}

	vars := mux.Vars(r)
	credentialID := vars["id"]

	if err := h.store.DeleteCredential(username, credentialID); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status": "success",
		"message": "device unbound successfully",
	})
}

func (h *Handler) GetUserProfile(w http.ResponseWriter, r *http.Request) {
	username, ok := middleware.GetUsernameFromContext(r.Context())
	if !ok {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}

	user, err := h.store.GetUser(username)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(user)
}

type webauthnUserWrapper struct {
	user        *models.User
	credentials []webauthn.Credential
}

func (u *webauthnUserWrapper) WebAuthnID() []byte {
	return []byte(u.user.ID)
}

func (u *webauthnUserWrapper) WebAuthnName() string {
	return u.user.Username
}

func (u *webauthnUserWrapper) WebAuthnDisplayName() string {
	return u.user.DisplayName
}

func (u *webauthnUserWrapper) WebAuthnCredentials() []webauthn.Credential {
	return u.credentials
}

func (u *webauthnUserWrapper) WebAuthnIcon() string {
	return ""
}
