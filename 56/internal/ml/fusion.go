package ml

import (
	"fmt"

	"iprep-sync/internal/config"
	"iprep-sync/internal/model"
)

func levelToScore(level model.ThreatLevel) int {
	switch level {
	case model.Level1:
		return 20
	case model.Level2:
		return 40
	case model.Level3:
		return 60
	case model.Level4:
		return 80
	case model.Level5:
		return 100
	default:
		return 0
	}
}

func scoreToLevel(score int) model.ThreatLevel {
	switch {
	case score >= 90:
		return model.Level5
	case score >= 70:
		return model.Level4
	case score >= 50:
		return model.Level3
	case score >= 30:
		return model.Level2
	case score >= 10:
		return model.Level1
	default:
		return model.LevelUnknown
	}
}

type FusionEngine struct {
	cfg config.ML
}

func NewFusionEngine(cfg config.ML) *FusionEngine {
	return &FusionEngine{cfg: cfg}
}

func (fe *FusionEngine) Fuse(bgpLevel model.ThreatLevel, mlScore *model.MLScore) *model.FusionResult {
	bgpWeight := fe.cfg.BGPWeight
	mlWeight := 1.0 - bgpWeight

	bgpNumeric := levelToScore(bgpLevel)

	mlNumeric := 0
	if mlScore != nil {
		mlNumeric = mlScore.ReputationScore
	}

	fused := float64(bgpNumeric)*bgpWeight + float64(mlNumeric)*mlWeight
	fusedScore := int(fused + 0.5)
	if fusedScore < 0 {
		fusedScore = 0
	}
	if fusedScore > 100 {
		fusedScore = 100
	}

	finalLevel := scoreToLevel(fusedScore)

	judgment := buildJudgment(bgpLevel, bgpWeight, mlNumeric, mlWeight, fusedScore, finalLevel, mlScore)

	return &model.FusionResult{
		BGPLevel:   bgpLevel,
		BGPWeight:  bgpWeight,
		MLScore:    mlNumeric,
		MLWeight:   mlWeight,
		FusedScore: fusedScore,
		FinalLevel: finalLevel,
		Judgment:   judgment,
	}
}

func buildJudgment(bgpLevel model.ThreatLevel, bgpWeight float64, mlScore int, mlWeight float64,
	fused int, finalLevel model.ThreatLevel, ml *model.MLScore) string {

	parts := ""

	if bgpLevel > model.LevelUnknown {
		parts += fmt.Sprintf("BGP情报标记等级=%d(权重%.0f%%) ", bgpLevel, bgpWeight*100)
	} else {
		parts += fmt.Sprintf("BGP情报无记录(权重%.0f%%) ", bgpWeight*100)
	}

	if ml != nil {
		parts += fmt.Sprintf("本地行为打分=%d(权重%.0f%%, 请求频率=%.2f/s, 失败率=%.1f%%) ",
			mlScore, mlWeight*100, ml.Features.RequestFreq, ml.Features.FailureRate*100)
	} else {
		parts += fmt.Sprintf("本地行为数据不足(权重%.0f%%) ", mlWeight*100)
	}

	parts += fmt.Sprintf("融合分数=%d, 最终等级=%d", fused, finalLevel)
	return parts
}

func (fe *FusionEngine) BGPWeight() float64 {
	return fe.cfg.BGPWeight
}

func (fe *FusionEngine) MinSamples() int {
	return fe.cfg.MinSamples
}
