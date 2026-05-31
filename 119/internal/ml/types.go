package ml

import (
	"time"
)

type QueryEngine string

const (
	EngineSpark  QueryEngine = "spark"
	EngineTrino  QueryEngine = "trino"
	EngineHive   QueryEngine = "hive"
	EnginePresto QueryEngine = "presto"
	EngineGeneric QueryEngine = "generic"
)

type QueryLogEntry struct {
	ID              string     `json:"id"`
	QueryText       string     `json:"query_text"`
	TableName       string     `json:"table_name"`
	Partitions      []string   `json:"partitions"`
	StartTime       time.Time  `json:"start_time"`
	EndTime         time.Time  `json:"end_time"`
	DurationMs      int64      `json:"duration_ms"`
	User            string     `json:"user"`
	Engine          QueryEngine `json:"engine"`
	Status          string     `json:"status"`
	BytesScanned    int64      `json:"bytes_scanned"`
	RowsRead        int64      `json:"rows_read"`
	FilesRead       int        `json:"files_read"`
	PartitionValues map[string]string `json:"partition_values"`
}

type PartitionQueryStats struct {
	TableName      string    `json:"table_name"`
	PartitionName  string    `json:"partition_name"`
	PartitionValue string    `json:"partition_value"`
	QueryCount     int64     `json:"query_count"`
	BytesScanned   int64     `json:"bytes_scanned"`
	TotalDurationMs int64    `json:"total_duration_ms"`
	AvgDurationMs  float64   `json:"avg_duration_ms"`
	LastQueryTime  time.Time `json:"last_query_time"`
	FirstQueryTime time.Time `json:"first_query_time"`
	HeatScore      float64   `json:"heat_score"`
	FileCount      int       `json:"file_count"`
	AvgFileSizeMB  float64   `json:"avg_file_size_mb"`
	TotalSizeMB    float64   `json:"total_size_mb"`
	ManifestCount  int       `json:"manifest_count"`
	IsHot          bool      `json:"is_hot"`
	IsCold         bool      `json:"is_cold"`
	IsWarm         bool      `json:"is_warm"`
}

type TableQueryStats struct {
	TableName    string              `json:"table_name"`
	TotalQueries int64               `json:"total_queries"`
	TotalBytes   int64               `json:"total_bytes"`
	Partitions   map[string]*PartitionQueryStats `json:"partitions"`
	HotPartitionCount  int           `json:"hot_partition_count"`
	ColdPartitionCount int           `json:"cold_partition_count"`
	WarmPartitionCount int           `json:"warm_partition_count"`
}

type TimeSeriesPoint struct {
	Timestamp time.Time `json:"timestamp"`
	Value     float64   `json:"value"`
}

type ForecastPoint struct {
	Timestamp      time.Time `json:"timestamp"`
	PredictedValue float64   `json:"predicted_value"`
	UpperBound     float64   `json:"upper_bound"`
	LowerBound     float64   `json:"lower_bound"`
	Confidence     float64   `json:"confidence"`
}

type ForecastResult struct {
	TableName       string          `json:"table_name"`
	PartitionName   string          `json:"partition_name"`
	ForecastHorizon string          `json:"forecast_horizon"`
	Historical      []TimeSeriesPoint `json:"historical"`
	Forecast        []ForecastPoint   `json:"forecast"`
	Trend           float64         `json:"trend"`
	Seasonality     float64         `json:"seasonality"`
	PredictedLoad   float64         `json:"predicted_load"`
	ShouldOptimize  bool            `json:"should_optimize"`
	OptimizeWindow  string          `json:"optimize_window"`
}

type OptimizationAction string

const (
	ActionCompactFiles   OptimizationAction = "compact_files"
	ActionMergeManifests OptimizationAction = "merge_manifests"
	ActionRewriteData    OptimizationAction = "rewrite_data"
	ActionRewriteManifest OptimizationAction = "rewrite_manifest"
	ActionSortData       OptimizationAction = "sort_data"
	ActionDeduplicate    OptimizationAction = "deduplicate"
	ActionNoOp           OptimizationAction = "no_op"
)

type OptimizationLevel string

const (
	LevelAggressive OptimizationLevel = "aggressive"
	LevelNormal     OptimizationLevel = "normal"
	LevelLight      OptimizationLevel = "light"
	LevelSkip       OptimizationLevel = "skip"
)

type OptimizationRecommendation struct {
	TableName      string             `json:"table_name"`
	PartitionName  string             `json:"partition_name"`
	PartitionValue string             `json:"partition_value"`
	Action         OptimizationAction `json:"action"`
	Level          OptimizationLevel  `json:"level"`
	TargetFileSizeMB int64            `json:"target_file_size_mb"`
	ManifestMergeThreshold int         `json:"manifest_merge_threshold"`
	Priority       int                `json:"priority"`
	Reason         string             `json:"reason"`
	HeatScore      float64            `json:"heat_score"`
	PredictedLoad  float64            `json:"predicted_load"`
	EstimatedBenefitMB int64          `json:"estimated_benefit_mb"`
	EstimatedTimeMs int64             `json:"estimated_time_ms"`
	Confidence     float64            `json:"confidence"`
}

type OptimizationStrategy struct {
	TableName        string                       `json:"table_name"`
	GeneratedAt      time.Time                    `json:"generated_at"`
	ModelVersion     string                       `json:"model_version"`
	LastTrainingTime time.Time                    `json:"last_training_time"`
	HeatThresholds   HeatThresholds               `json:"heat_thresholds"`
	Recommendations  []OptimizationRecommendation `json:"recommendations"`
	HotPartitions    []string                     `json:"hot_partitions"`
	ColdPartitions   []string                     `json:"cold_partitions"`
	ForecastSummary  map[string]float64           `json:"forecast_summary"`
	TotalActionCount int                          `json:"total_action_count"`
	EstimatedTotalBenefitMB int64                 `json:"estimated_total_benefit_mb"`
}

type HeatThresholds struct {
	HotQueryCount     int64   `json:"hot_query_count"`
	HotBytesThreshold int64   `json:"hot_bytes_threshold"`
	HotHeatScore      float64 `json:"hot_heat_score"`
	ColdQueryCount    int64   `json:"cold_query_count"`
	ColdBytesThreshold int64  `json:"cold_bytes_threshold"`
	ColdHeatScore     float64 `json:"cold_heat_score"`
	WarmHeatScoreMin  float64 `json:"warm_heat_score_min"`
	WarmHeatScoreMax  float64 `json:"warm_heat_score_max"`
	LookbackDays      int     `json:"lookback_days"`
}

type ModelConfig struct {
	ModelPath       string        `json:"model_path"`
	LookbackDays    int           `json:"lookback_days"`
	ForecastDays    int           `json:"forecast_days"`
	SeasonalityPeriod string      `json:"seasonality_period"`
	RetrainInterval time.Duration `json:"retrain_interval"`
	MinDataPoints   int           `json:"min_data_points"`
	HeatThresholds  HeatThresholds `json:"heat_thresholds"`
}

type OptimizationEngine struct {
	config       ModelConfig
	forecaster   *ProphetForecaster
	detector     *HeatDetector
	analyzer     *QueryAnalyzer
	recommender  *RecommendationEngine
	modelVersion string
}

func DefaultModelConfig() ModelConfig {
	return ModelConfig{
		LookbackDays:     30,
		ForecastDays:     1,
		SeasonalityPeriod: "daily",
		RetrainInterval:  168 * time.Hour,
		MinDataPoints:    24,
		HeatThresholds: HeatThresholds{
			HotQueryCount:     100,
			HotBytesThreshold: 10 * 1024 * 1024 * 1024,
			HotHeatScore:      0.7,
			ColdQueryCount:    1,
			ColdBytesThreshold: 10 * 1024 * 1024,
			ColdHeatScore:     0.1,
			WarmHeatScoreMin:  0.1,
			WarmHeatScoreMax:  0.7,
			LookbackDays:      7,
		},
	}
}
