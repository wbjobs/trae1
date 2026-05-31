package ml

import (
	"fmt"
	"sort"
	"time"
)

type RecommendationEngine struct {
	verbose bool
	config  ModelConfig
}

func NewRecommendationEngine(verbose bool, config ModelConfig) *RecommendationEngine {
	return &RecommendationEngine{
		verbose: verbose,
		config:  config,
	}
}

func (e *RecommendationEngine) GenerateRecommendations(
	tableStats *TableQueryStats,
	forecasts map[string]*ForecastResult,
	currentFileSizeMB int64,
	currentManifestCount int,
	currentFileCount int,
) []OptimizationRecommendation {
	var recommendations []OptimizationRecommendation

	if e.verbose {
		fmt.Printf("[ml] generating recommendations for table %s (%d partitions)\n",
			tableStats.TableName, len(tableStats.Partitions))
	}

	for partKey, partStats := range tableStats.Partitions {
		rec := e.generatePartitionRecommendation(
			tableStats.TableName,
			partKey,
			partStats,
			forecasts[partKey],
			currentFileSizeMB,
			currentManifestCount,
			currentFileCount,
		)
		if rec.Action != ActionNoOp {
			recommendations = append(recommendations, rec)
		}
	}

	sort.Slice(recommendations, func(i, j int) bool {
		if recommendations[i].Priority != recommendations[j].Priority {
			return recommendations[i].Priority < recommendations[j].Priority
		}
		return recommendations[i].HeatScore > recommendations[j].HeatScore
	})

	return recommendations
}

func (e *RecommendationEngine) generatePartitionRecommendation(
	tableName string,
	partKey string,
	partStats *PartitionQueryStats,
	forecast *ForecastResult,
	currentFileSizeMB int64,
	currentManifestCount int,
	currentFileCount int,
) OptimizationRecommendation {
	rec := OptimizationRecommendation{
		TableName:      tableName,
		PartitionName:  partStats.PartitionName,
		PartitionValue: partStats.PartitionValue,
		HeatScore:      partStats.HeatScore,
		Confidence:     0.7,
	}

	if forecast != nil {
		rec.PredictedLoad = forecast.PredictedLoad
	}

	switch {
	case partStats.IsHot:
		rec = e.buildHotRecommendation(rec, partStats, forecast, currentFileSizeMB, currentManifestCount)
	case partStats.IsCold:
		rec = e.buildColdRecommendation(rec, partStats, currentFileCount)
	default:
		rec = e.buildWarmRecommendation(rec, partStats, forecast, currentFileSizeMB, currentManifestCount)
	}

	rec = e.estimateBenefits(rec, partStats)
	return rec
}

func (e *RecommendationEngine) buildHotRecommendation(
	rec OptimizationRecommendation,
	partStats *PartitionQueryStats,
	forecast *ForecastResult,
	currentFileSizeMB int64,
	currentManifestCount int,
) OptimizationRecommendation {
	rec.Level = LevelAggressive
	rec.Priority = 1
	rec.Confidence = 0.85

	hotTargetFileSize := int64(1024)
	rec.TargetFileSizeMB = hotTargetFileSize

	hotManifestThreshold := 16
	rec.ManifestMergeThreshold = hotManifestThreshold

	needsFileCompact := currentFileSizeMB > 0 && currentFileSizeMB < 256
	needsManifestMerge := currentManifestCount > hotManifestThreshold

	if needsFileCompact && needsManifestMerge {
		rec.Action = ActionCompactFiles
		rec.Reason = fmt.Sprintf(
			"Hot partition %s: high query load detected (heat_score=%.2f, queries=%d, avg_file_size=%dMB < 256MB). Recommend aggressive compaction to 1GB file size and merge manifests (current: %d > threshold %d)",
			partStats.PartitionName, partStats.HeatScore, partStats.QueryCount,
			currentFileSizeMB, currentManifestCount, hotManifestThreshold)
	} else if needsFileCompact {
		rec.Action = ActionRewriteData
		rec.Reason = fmt.Sprintf(
			"Hot partition %s: small files detected (avg %dMB). Recommend rewrite to 1GB file size for better scan performance",
			partStats.PartitionName, currentFileSizeMB)
	} else if needsManifestMerge {
		rec.Action = ActionMergeManifests
		rec.Reason = fmt.Sprintf(
			"Hot partition %s: too many manifests (%d > %d). Recommend merging to reduce metadata overhead",
			partStats.PartitionName, currentManifestCount, hotManifestThreshold)
	} else {
		rec.Action = ActionCompactFiles
		rec.Reason = fmt.Sprintf(
			"Hot partition %s: proactive aggressive optimization recommended due to high query load (heat_score=%.2f)",
			partStats.PartitionName, partStats.HeatScore)
	}

	if forecast != nil && forecast.ShouldOptimize {
		rec.Reason += fmt.Sprintf(". Forecasted query load will peak at %.0f q/h in next 24h. Recommended window: %s",
			forecast.PredictedLoad, forecast.OptimizeWindow)
	}

	return rec
}

func (e *RecommendationEngine) buildColdRecommendation(
	rec OptimizationRecommendation,
	partStats *PartitionQueryStats,
	currentFileCount int,
) OptimizationRecommendation {
	rec.Level = LevelSkip
	rec.Priority = 100
	rec.Confidence = 0.6

	if currentFileCount > 128 {
		rec.Level = LevelLight
		rec.Action = ActionMergeManifests
		rec.Priority = 50
		rec.TargetFileSizeMB = 512
		rec.ManifestMergeThreshold = 64
		rec.Reason = fmt.Sprintf(
			"Cold partition %s: very low query load (heat_score=%.2f, queries=%d). Many files (%d) warrant light compaction only",
			partStats.PartitionName, partStats.HeatScore, partStats.QueryCount, currentFileCount)
	} else {
		rec.Action = ActionNoOp
		rec.Reason = fmt.Sprintf(
			"Cold partition %s: low query activity (heat_score=%.2f). No optimization needed - skip to save resources",
			partStats.PartitionName, partStats.HeatScore)
	}

	return rec
}

func (e *RecommendationEngine) buildWarmRecommendation(
	rec OptimizationRecommendation,
	partStats *PartitionQueryStats,
	forecast *ForecastResult,
	currentFileSizeMB int64,
	currentManifestCount int,
) OptimizationRecommendation {
	rec.Level = LevelNormal
	rec.Priority = 10
	rec.Confidence = 0.75

	warmTargetFileSize := int64(512)
	rec.TargetFileSizeMB = warmTargetFileSize

	warmManifestThreshold := 32
	rec.ManifestMergeThreshold = warmManifestThreshold

	needsFileCompact := currentFileSizeMB > 0 && currentFileSizeMB < 128
	needsManifestMerge := currentManifestCount > warmManifestThreshold

	if needsFileCompact && needsManifestMerge {
		rec.Action = ActionCompactFiles
		rec.Reason = fmt.Sprintf(
			"Warm partition %s: moderate query load (heat_score=%.2f). Recommend normal compaction to 512MB and merge manifests (%d > %d)",
			partStats.PartitionName, partStats.HeatScore,
			currentManifestCount, warmManifestThreshold)
	} else if needsFileCompact {
		rec.Action = ActionRewriteData
		rec.Reason = fmt.Sprintf(
			"Warm partition %s: small files (%dMB < 128MB). Recommend rewrite to 512MB target size",
			partStats.PartitionName, currentFileSizeMB)
	} else if needsManifestMerge {
		rec.Action = ActionMergeManifests
		rec.Reason = fmt.Sprintf(
			"Warm partition %s: manifest count above threshold (%d > %d). Merge recommended",
			partStats.PartitionName, currentManifestCount, warmManifestThreshold)
	} else {
		rec.Action = ActionNoOp
		rec.Reason = fmt.Sprintf(
			"Warm partition %s: query load moderate (heat_score=%.2f). Current file sizes and manifest count are acceptable",
			partStats.PartitionName, partStats.HeatScore)
	}

	if forecast != nil && forecast.ShouldOptimize {
		rec.Action = ActionCompactFiles
		rec.Reason += fmt.Sprintf(". Forecast shows load increase (predicted=%.0f q/h). Proactive optimization recommended",
			forecast.PredictedLoad)
	}

	return rec
}

func (e *RecommendationEngine) estimateBenefits(
	rec OptimizationRecommendation,
	partStats *PartitionQueryStats,
) OptimizationRecommendation {
	if rec.Action == ActionNoOp {
		return rec
	}

	var benefitMB int64
	switch rec.Level {
	case LevelAggressive:
		benefitMB = int64(float64(partStats.TotalSizeMB) * 0.3)
	case LevelNormal:
		benefitMB = int64(float64(partStats.TotalSizeMB) * 0.15)
	case LevelLight:
		benefitMB = int64(float64(partStats.TotalSizeMB) * 0.05)
	}

	var timeMs int64
	switch rec.Action {
	case ActionCompactFiles, ActionRewriteData:
		timeMs = int64(float64(partStats.TotalSizeMB) * 0.5)
	case ActionMergeManifests:
		timeMs = int64(partStats.ManifestCount) * 100
	case ActionRewriteManifest:
		timeMs = int64(partStats.ManifestCount) * 50
	default:
		timeMs = 1000
	}

	rec.EstimatedBenefitMB = benefitMB
	rec.EstimatedTimeMs = timeMs

	return rec
}

func (e *RecommendationEngine) BuildStrategy(
	tableName string,
	tableStats *TableQueryStats,
	recommendations []OptimizationRecommendation,
	forecasts map[string]*ForecastResult,
) *OptimizationStrategy {
	strategy := &OptimizationStrategy{
		TableName:        tableName,
		GeneratedAt:      time.Now(),
		ModelVersion:     "1.0.0",
		LastTrainingTime: time.Now(),
		HeatThresholds:   e.config.HeatThresholds,
		Recommendations:  recommendations,
		ForecastSummary:  map[string]float64{},
	}

	for key, p := range tableStats.Partitions {
		if p.IsHot {
			strategy.HotPartitions = append(strategy.HotPartitions, key)
		}
		if p.IsCold {
			strategy.ColdPartitions = append(strategy.ColdPartitions, key)
		}
	}

	strategy.TotalActionCount = len(recommendations)

	var totalBenefit int64
	for _, r := range recommendations {
		totalBenefit += r.EstimatedBenefitMB
	}
	strategy.EstimatedTotalBenefitMB = totalBenefit

	for partKey, forecast := range forecasts {
		if forecast != nil {
			strategy.ForecastSummary[partKey] = forecast.PredictedLoad
		}
	}

	return strategy
}
