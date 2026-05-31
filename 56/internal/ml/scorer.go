package ml

import (
	"fmt"
	"log"
	"sync"
	"time"

	"iprep-sync/internal/config"
	"iprep-sync/internal/model"
)

type Scorer struct {
	cfg        config.ML
	modelLoaded bool
	mu         sync.RWMutex
}

func NewScorer(cfg config.ML) (*Scorer, error) {
	s := &Scorer{cfg: cfg}
	if !cfg.Enabled {
		log.Printf("[ml] ML scoring disabled")
		return s, nil
	}
	if cfg.ModelPath == "" {
		log.Printf("[ml] no model path configured, ML scoring disabled")
		return s, nil
	}
	loaded, err := loadModel(cfg.ModelPath)
	if err != nil {
		log.Printf("[ml] failed to load ONNX model from %s: %v (ML scoring will use fallback heuristic)", cfg.ModelPath, err)
		return s, nil
	}
	s.modelLoaded = loaded
	return s, nil
}

func (s *Scorer) Enabled() bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.cfg.Enabled && s.modelLoaded
}

func (s *Scorer) ModelPath() string {
	return s.cfg.ModelPath
}

func (s *Scorer) Score(features model.BehaviorFeatures) (*model.MLScore, error) {
	if features.RequestCount < s.cfg.MinSamples {
		return nil, fmt.Errorf("insufficient samples: %d < %d", features.RequestCount, s.cfg.MinSamples)
	}

	featureVec := featuresToVector(features)

	var anomalyScore float64
	if s.Enabled() {
		score, err := runInference(featureVec)
		if err != nil {
			log.Printf("[ml] ONNX inference failed, falling back to heuristic: %v", err)
			anomalyScore = heuristicScore(features)
		} else {
			anomalyScore = score
		}
	} else {
		anomalyScore = heuristicScore(features)
	}

	reputationScore := anomalyToReputation(anomalyScore)

	return &model.MLScore{
		AnomalyScore:    anomalyScore,
		ReputationScore: reputationScore,
		Features:        features,
		ModelUsed:       s.cfg.ModelPath,
		ScoredAt:        time.Now().UTC(),
	}, nil
}

func featuresToVector(f model.BehaviorFeatures) []float32 {
	return []float32{
		float32(f.RequestFreq),
		float32(f.FailureRate),
		float32(f.PathEntropy),
		float32(f.UniquePaths),
		float32(f.DaytimeRatio),
		float32(f.NighttimeRatio),
		float32(f.PeakHour) / 24.0,
		float32(f.RequestCount),
	}
}

func anomalyToReputation(anomalyScore float64) int {
	if anomalyScore < 0 {
		anomalyScore = 0
	}
	if anomalyScore > 1 {
		anomalyScore = 1
	}
	score := int(anomalyScore * 100)
	if score < 0 {
		score = 0
	}
	if score > 100 {
		score = 100
	}
	return score
}

func heuristicScore(f model.BehaviorFeatures) float64 {
	score := 0.0

	if f.FailureRate > 0.5 {
		score += 0.3
	} else if f.FailureRate > 0.2 {
		score += 0.15
	}

	if f.RequestFreq > 10 {
		score += 0.25
	} else if f.RequestFreq > 1 {
		score += 0.1
	}

	if f.NighttimeRatio > 0.7 {
		score += 0.2
	}

	if f.PathEntropy < 0.5 && f.RequestCount > 50 {
		score += 0.15
	}

	if score > 1.0 {
		score = 1.0
	}
	return score
}
