package risk

import (
	"encoding/json"
	"fmt"
	"net/http"
	"sync"
	"time"

	"webauthn-auth/internal/threatintel"
)

type RiskEngine struct {
	mu            sync.RWMutex
	forest        *IsolationForest
	historyStore  *HistoryStore
	threatIntel   *threatintel.ThreatIntelClient
	lastTrainTime time.Time
	trainInterval time.Duration
	enabled       bool
}

type RiskAssessment struct {
	Score          float64            `json:"score"`
	Level          RiskLevel          `json:"level"`
	LevelName      string             `json:"levelName"`
	Context        *ContextInfo       `json:"context"`
	Factors        []RiskFactor       `json:"factors"`
	Action         RequiredAction     `json:"action"`
	ThreatIntel    *threatintel.IPReport `json:"threatIntel,omitempty"`
	Timestamp      time.Time          `json:"timestamp"`
}

type RiskFactor struct {
	Name        string  `json:"name"`
	Description string  `json:"description"`
	Weight      float64 `json:"weight"`
	Impact      float64 `json:"impact"`
	Severity    string  `json:"severity"`
}

type RequiredAction struct {
	Type        string   `json:"type"`
	Description string   `json:"description"`
	Methods     []string `json:"methods,omitempty"`
}

type Config struct {
	Enabled       bool
	TrainInterval time.Duration
	NumTrees      int
	SampleSize    int
	ThreatIntelAPI string
	ThreatIntelKey string
}

func DefaultConfig() *Config {
	return &Config{
		Enabled:       true,
		TrainInterval: 7 * 24 * time.Hour,
		NumTrees:      100,
		SampleSize:    256,
	}
}

func NewRiskEngine(config *Config) *RiskEngine {
	if config == nil {
		config = DefaultConfig()
	}

	engine := &RiskEngine{
		forest:        NewIsolationForest(config.NumTrees, config.SampleSize),
		historyStore:  NewHistoryStore(),
		lastTrainTime: time.Now(),
		trainInterval: config.TrainInterval,
		enabled:       config.Enabled,
	}

	if config.ThreatIntelAPI != "" {
		engine.threatIntel = threatintel.NewThreatIntelClient(config.ThreatIntelAPI, config.ThreatIntelKey)
	}

	return engine
}

func (e *RiskEngine) AssessRisk(r *http.Request, username string) *RiskAssessment {
	e.mu.RLock()
	defer e.mu.RUnlock()

	history := e.historyStore.GetOrCreate(username)
	ctx := CollectContext(r, username, history)

	assessment := &RiskAssessment{
		Context:   ctx,
		Timestamp: time.Now(),
	}

	if !e.enabled {
		assessment.Score = 0
		assessment.Level = RiskLow
		assessment.LevelName = RiskLow.String()
		assessment.Action = RequiredAction{
			Type:        "allow",
			Description: "Risk assessment disabled",
		}
		return assessment
	}

	features := ctx.ToFeatures()
	point := DataPoint{
		Features: features,
		Label:    username,
	}

	modelScore := e.forest.AnomalyScore(point)
	assessment.Score = calculateFinalScore(modelScore, ctx, history)
	assessment.Level = ClassifyRisk(assessment.Score)
	assessment.LevelName = assessment.Level.String()

	assessment.Factors = identifyRiskFactors(ctx, history)

	if e.threatIntel != nil && ctx.IPAddress != "" {
		report, err := e.threatIntel.LookupIP(ctx.IPAddress)
		if err == nil {
			assessment.ThreatIntel = report
			if report.IsMalicious {
				assessment.Score += 20
				assessment.Score = min(assessment.Score, 100)
				assessment.Level = ClassifyRisk(assessment.Score)
				assessment.LevelName = assessment.Level.String()
			}
		}
	}

	assessment.Action = determineRequiredAction(assessment.Level, ctx, history)

	return assessment
}

func calculateFinalScore(modelScore float64, ctx *ContextInfo, history *UserHistory) float64 {
	score := modelScore * 0.5

	if history.HasUnusualLocation(ctx.IPAddress, ctx.Country) {
		score += 15
	}
	if history.HasUnusualDevice(ctx.DeviceFingerprint) {
		score += 15
	}
	if ctx.FailedAttempts > 2 {
		score += 10
	}
	if history.IsNewUser() {
		score += 10
	}
	if ctx.BehaviorScore > 0.5 {
		score += 10
	}

	return min(score, 100)
}

func identifyRiskFactors(ctx *ContextInfo, history *UserHistory) []RiskFactor {
	var factors []RiskFactor

	if history.HasUnusualLocation(ctx.IPAddress, ctx.Country) {
		factors = append(factors, RiskFactor{
			Name:        "unusual_location",
			Description: fmt.Sprintf("Login from unusual location: %s (%s)", ctx.IPAddress, ctx.Country),
			Weight:      0.25,
			Impact:      15,
			Severity:    "high",
		})
	}

	if history.HasUnusualDevice(ctx.DeviceFingerprint) {
		factors = append(factors, RiskFactor{
			Name:        "unusual_device",
			Description: fmt.Sprintf("Login from new device: %s on %s", ctx.Browser, ctx.OS),
			Weight:      0.25,
			Impact:      15,
			Severity:    "high",
		})
	}

	if ctx.HourOfDay >= 1 && ctx.HourOfDay < 5 {
		factors = append(factors, RiskFactor{
			Name:        "unusual_hour",
			Description: fmt.Sprintf("Login during unusual hours: %d:00", ctx.HourOfDay),
			Weight:      0.15,
			Impact:      10,
			Severity:    "medium",
		})
	}

	if ctx.FailedAttempts > 2 {
		factors = append(factors, RiskFactor{
			Name:        "multiple_failures",
			Description: fmt.Sprintf("%d previous failed attempts", ctx.FailedAttempts),
			Weight:      0.15,
			Impact:      10,
			Severity:    "medium",
		})
	}

	if history.IsNewUser() {
		factors = append(factors, RiskFactor{
			Name:        "new_user",
			Description: fmt.Sprintf("New user with only %d logins", history.TotalLogins),
			Weight:      0.10,
			Impact:      10,
			Severity:    "low",
		})
	}

	if ctx.BehaviorScore > 0.5 {
		factors = append(factors, RiskFactor{
			Name:        "behavior_anomaly",
			Description: fmt.Sprintf("Behavior deviation score: %.2f", ctx.BehaviorScore),
			Weight:      0.10,
			Impact:      10,
			Severity:    "low",
		})
	}

	return factors
}

func determineRequiredAction(level RiskLevel, ctx *ContextInfo, history *UserHistory) RequiredAction {
	switch level {
	case RiskLow:
		return RequiredAction{
			Type:        "allow",
			Description: "Low risk - proceed with normal authentication",
		}

	case RiskMedium:
		return RequiredAction{
			Type:        "enhanced_verification",
			Description: "Medium risk - enhanced verification required",
			Methods:     []string{"webauthn_gesture", "device_biometric"},
		}

	case RiskHigh:
		return RequiredAction{
			Type:        "additional_verification",
			Description: "High risk - additional verification required",
			Methods:     []string{"email_otp", "sms_otp", "security_questions"},
		}

	case RiskCritical:
		return RequiredAction{
			Type:        "block_or_admin",
			Description: "Critical risk - admin approval required or account locked",
			Methods:     []string{"admin_approval", "manual_review"},
		}

	default:
		return RequiredAction{
			Type:        "allow",
			Description: "Unknown risk level - proceeding",
		}
	}
}

func (e *RiskEngine) RecordLogin(username string, ctx *ContextInfo, success bool) {
	e.historyStore.Update(username, ctx, success)
}

func (e *RiskEngine) ShouldRetrain() bool {
	e.mu.RLock()
	defer e.mu.RUnlock()
	return time.Since(e.lastTrainTime) > e.trainInterval
}

func (e *RiskEngine) Retrain() error {
	e.mu.Lock()
	defer e.mu.Unlock()

	trainingData := e.historyStore.GenerateTrainingData()

	if len(trainingData) < 10 {
		return fmt.Errorf("insufficient training data: %d samples, need at least 10", len(trainingData))
	}

	e.forest.Train(trainingData)
	e.lastTrainTime = time.Now()

	return nil
}

func (e *RiskEngine) GetHistory(username string) (*UserHistory, bool) {
	return e.historyStore.Get(username)
}

func (e *RiskEngine) GetStats() map[string]interface{} {
	e.mu.RLock()
	defer e.mu.RUnlock()

	return map[string]interface{}{
		"enabled":         e.enabled,
		"numTrees":        e.forest.numTrees,
		"sampleSize":      e.forest.sampleSize,
		"totalUsers":      len(e.historyStore.histories),
		"lastTrainTime":   e.lastTrainTime,
		"nextTrainTime":   e.lastTrainTime.Add(e.trainInterval),
		"trainInterval":   e.trainInterval.String(),
		"threatIntel":     e.threatIntel != nil,
	}
}

func (e *RiskEngine) ToJSON() string {
	stats := e.GetStats()
	data, _ := json.Marshal(stats)
	return string(data)
}

func (e *RiskEngine) SetEnabled(enabled bool) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.enabled = enabled
}

func min(a, b float64) float64 {
	if a < b {
		return a
	}
	return b
}
