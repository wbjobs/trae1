package predict

import (
	"encoding/json"
	"fmt"
	"math"
	"time"
)

type MigrationMode int

const (
	ModePreCopy  MigrationMode = iota
	ModePostCopy
	ModeDirect
)

func (m MigrationMode) String() string {
	switch m {
	case ModePreCopy:
		return "pre-copy"
	case ModePostCopy:
		return "post-copy"
	case ModeDirect:
		return "direct"
	default:
		return "unknown"
	}
}

type PredictionResult struct {
	ContainerName     string        `json:"container_name"`
	Timestamp         time.Time     `json:"timestamp"`

	RecommendedMode   MigrationMode `json:"recommended_mode"`
	PreCopyIters      int           `json:"precopy_iters"`
	EstimatedTotalTime float64      `json:"estimated_total_time_sec"`
	EstimatedDowntime float64       `json:"estimated_downtime_ms"`
	SuccessProbability float64      `json:"success_probability"`

	DirtyRate         float64       `json:"dirty_rate_bytes_per_sec"`
	TotalMemory       int64         `json:"total_memory_bytes"`
	NetworkBandwidth  float64       `json:"network_bandwidth_bytes_per_sec"`

	PreCopyAnalysis   *PreCopyAnalysis  `json:"precopy_analysis,omitempty"`
	PostCopyAnalysis  *PostCopyAnalysis `json:"postcopy_analysis,omitempty"`
	DirectAnalysis    *DirectAnalysis   `json:"direct_analysis,omitempty"`

	ModelVersion      string        `json:"model_version"`
	TrainingSamples   int           `json:"training_samples"`
}

type PreCopyAnalysis struct {
	Feasible          bool    `json:"feasible"`
	OptimalIterations int     `json:"optimal_iterations"`
	TotalTransferMB   float64 `json:"total_transfer_mb"`
	DowntimeMs        float64 `json:"downtime_ms"`
	Reason            string  `json:"reason,omitempty"`
}

type PostCopyAnalysis struct {
	Feasible       bool    `json:"feasible"`
	PageFaultRate  float64 `json:"estimated_page_fault_rate_per_sec"`
	RecoveryTimeMs float64 `json:"estimated_recovery_time_ms"`
	Reason         string  `json:"reason,omitempty"`
}

type DirectAnalysis struct {
	Feasible    bool    `json:"feasible"`
	TransferMB  float64 `json:"transfer_mb"`
	DowntimeMs  float64 `json:"downtime_ms"`
	Reason      string  `json:"reason,omitempty"`
}

type LinearModel struct {
	Weights      []float64 `json:"weights"`
	Bias         float64   `json:"bias"`
	FeatureNames []string  `json:"feature_names"`
	TrainedOn    int       `json:"trained_on_samples"`
	LastUpdated  time.Time `json:"last_updated"`
}

type Predictor struct {
	totalTimeModel  *LinearModel
	downtimeModel   *LinearModel
	successModel    *LinearModel
	history         []MigrationRecord
	modeThreshold   float64
}

func NewPredictor() *Predictor {
	return &Predictor{
		totalTimeModel: defaultTotalTimeModel(),
		downtimeModel:  defaultDowntimeModel(),
		successModel:   defaultSuccessModel(),
		history:        make([]MigrationRecord, 0),
		modeThreshold:  0.8,
	}
}

func defaultTotalTimeModel() *LinearModel {
	return &LinearModel{
		Weights:      []float64{0.001, 0.5, 0.0001, 0.1, 0.05},
		Bias:         5.0,
		FeatureNames: []string{"dirty_rate_ratio", "total_memory_mb", "bandwidth_mbps", "num_iters", "container_size_mb"},
		TrainedOn:    100,
	}
}

func defaultDowntimeModel() *LinearModel {
	return &LinearModel{
		Weights:      []float64{0.5, 0.01, 10.0, 5.0},
		Bias:         50.0,
		FeatureNames: []string{"final_dirty_mb", "transfer_speed_mbps", "iterations", "has_gpu"},
		TrainedOn:    100,
	}
}

func defaultSuccessModel() *LinearModel {
	return &LinearModel{
		Weights:      []float64{-0.1, 0.2, 0.3, -0.15, 0.1},
		Bias:         0.85,
		FeatureNames: []string{"dirty_rate_ratio", "bandwidth_ratio", "memory_headroom", "cpu_load", "network_stability"},
		TrainedOn:    100,
	}
}

func (p *Predictor) Predict(containerName string, dirtyRate float64, totalMemory int64,
	networkBandwidth float64, hasGPU bool) *PredictionResult {

	result := &PredictionResult{
		ContainerName:    containerName,
		Timestamp:        time.Now(),
		DirtyRate:        dirtyRate,
		TotalMemory:      totalMemory,
		NetworkBandwidth: networkBandwidth,
		ModelVersion:     "v1.0",
		TrainingSamples:  p.totalTimeModel.TrainedOn,
	}

	totalMemoryMB := float64(totalMemory) / (1024 * 1024)
	bandwidthMBs := networkBandwidth / (1024 * 1024)
	dirtyRateMBs := dirtyRate / (1024 * 1024)
	dirtyRateRatio := 0.0
	if bandwidthMBs > 0 {
		dirtyRateRatio = dirtyRateMBs / bandwidthMBs
	}

	preCopyAnalysis := p.analyzePreCopy(totalMemoryMB, dirtyRateMBs, bandwidthMBs, dirtyRateRatio, hasGPU)
	result.PreCopyAnalysis = preCopyAnalysis

	postCopyAnalysis := p.analyzePostCopy(totalMemoryMB, dirtyRateMBs, bandwidthMBs, dirtyRateRatio, hasGPU)
	result.PostCopyAnalysis = postCopyAnalysis

	directAnalysis := p.analyzeDirect(totalMemoryMB, bandwidthMBs, hasGPU)
	result.DirectAnalysis = directAnalysis

	bestMode, bestIters := p.selectMode(preCopyAnalysis, postCopyAnalysis, directAnalysis, dirtyRateRatio)
	result.RecommendedMode = bestMode
	result.PreCopyIters = bestIters

	features := p.buildFeatures(dirtyRateRatio, totalMemoryMB, bandwidthMBs, bestIters, hasGPU)
	result.EstimatedTotalTime = p.totalTimeModel.Predict(features)
	result.EstimatedDowntime = p.downtimeModel.Predict(features)

	successFeatures := p.buildSuccessFeatures(dirtyRateRatio, totalMemoryMB, bandwidthMBs)
	result.SuccessProbability = p.successModel.Predict(successFeatures)
	result.SuccessProbability = clamp(result.SuccessProbability, 0, 1)

	return result
}

func (p *Predictor) analyzePreCopy(totalMemoryMB, dirtyRateMBs, bandwidthMBs, dirtyRateRatio float64, hasGPU bool) *PreCopyAnalysis {
	analysis := &PreCopyAnalysis{}

	if dirtyRateRatio > 1.0 {
		analysis.Feasible = false
		analysis.Reason = fmt.Sprintf("脏页速率(%.1f MB/s)超过带宽(%.1f MB/s)，预拷贝无法收敛", dirtyRateMBs, bandwidthMBs)
		return analysis
	}

	if dirtyRateRatio > p.modeThreshold {
		analysis.Feasible = false
		analysis.Reason = fmt.Sprintf("脏页/带宽比(%.2f)过高，预拷贝效率低", dirtyRateRatio)
		return analysis
	}

	analysis.Feasible = true

	optimalIters := 3
	if dirtyRateRatio < 0.1 {
		optimalIters = 2
	} else if dirtyRateRatio < 0.3 {
		optimalIters = 3
	} else if dirtyRateRatio < 0.5 {
		optimalIters = 4
	} else {
		optimalIters = 5
	}
	analysis.OptimalIterations = optimalIters

	firstIterMB := totalMemoryMB
	remainingDirtyMB := totalMemoryMB * math.Pow(dirtyRateRatio, float64(optimalIters))
	analysis.TotalTransferMB = firstIterMB + (float64(optimalIters-1) * remainingDirtyMB)

	finalDirtyMB := totalMemoryMB * math.Pow(dirtyRateRatio, float64(optimalIters))
	analysis.DowntimeMs = (finalDirtyMB / bandwidthMBs) * 1000
	if hasGPU {
		analysis.DowntimeMs += 200
	}
	analysis.DowntimeMs += 50

	return analysis
}

func (p *Predictor) analyzePostCopy(totalMemoryMB, dirtyRateMBs, bandwidthMBs, dirtyRateRatio float64, hasGPU bool) *PostCopyAnalysis {
	analysis := &PostCopyAnalysis{}

	if dirtyRateRatio < 0.3 {
		analysis.Feasible = false
		analysis.Reason = fmt.Sprintf("脏页速率低(%.1f MB/s)，后拷贝优势不明显", dirtyRateMBs)
		return analysis
	}

	analysis.Feasible = true
	analysis.PageFaultRate = dirtyRateMBs * 0.8
	analysis.RecoveryTimeMs = (totalMemoryMB / bandwidthMBs) * 1000
	if hasGPU {
		analysis.RecoveryTimeMs += 500
	}

	return analysis
}

func (p *Predictor) analyzeDirect(totalMemoryMB, bandwidthMBs float64, hasGPU bool) *DirectAnalysis {
	analysis := &DirectAnalysis{}

	analysis.Feasible = true
	analysis.TransferMB = totalMemoryMB
	analysis.DowntimeMs = (totalMemoryMB / bandwidthMBs) * 1000
	if hasGPU {
		analysis.DowntimeMs += 300
	}

	if totalMemoryMB > 1024 && bandwidthMBs < 100 {
		analysis.DowntimeMs += 5000
	}

	return analysis
}

func (p *Predictor) selectMode(preCopy *PreCopyAnalysis, postCopy *PostCopyAnalysis,
	direct *DirectAnalysis, dirtyRateRatio float64) (MigrationMode, int) {

	if preCopy.Feasible && preCopy.DowntimeMs < 100 {
		return ModePreCopy, preCopy.OptimalIterations
	}

	if preCopy.Feasible && preCopy.DowntimeMs < direct.DowntimeMs {
		return ModePreCopy, preCopy.OptimalIterations
	}

	if dirtyRateRatio > 1.0 && postCopy.Feasible {
		return ModePostCopy, 0
	}

	if !preCopy.Feasible && postCopy.Feasible {
		return ModePostCopy, 0
	}

	return ModeDirect, 0
}

func (p *Predictor) buildFeatures(dirtyRateRatio, totalMemoryMB, bandwidthMBs float64,
	iters int, hasGPU float64) []float64 {

	hasGPUFloat := 0.0
	if hasGPU > 0 {
		hasGPUFloat = 1.0
	}

	return []float64{
		dirtyRateRatio,
		totalMemoryMB,
		bandwidthMBs,
		float64(iters),
		hasGPUFloat,
	}
}

func (p *Predictor) buildSuccessFeatures(dirtyRateRatio, totalMemoryMB, bandwidthMBs float64) []float64 {
	memoryHeadroom := 1.0
	if totalMemoryMB > 8192 {
		memoryHeadroom = 0.8
	}

	return []float64{
		dirtyRateRatio,
		bandwidthMBs / 1000,
		memoryHeadroom,
		0.5,
		0.9,
	}
}

func (m *LinearModel) Predict(features []float64) float64 {
	if len(features) != len(m.Weights) {
		return m.Bias
	}

	result := m.Bias
	for i, w := range m.Weights {
		result += w * features[i]
	}
	return result
}

func (m *LinearModel) Update(features []float64, actualValue float64, learningRate float64) {
	if len(features) != len(m.Weights) {
		return
	}

	predicted := m.Predict(features)
	error := actualValue - predicted

	for i := range m.Weights {
		m.Weights[i] += learningRate * error * features[i]
	}
	m.Bias += learningRate * error
	m.TrainedOn++
	m.LastUpdated = time.Now()
}

func clamp(v, min, max float64) float64 {
	if v < min {
		return min
	}
	if v > max {
		return max
	}
	return v
}

func (r *PredictionResult) RecommendationText() string {
	text := fmt.Sprintf(`
=== 迁移预测报告 ===
容器: %s
时间: %s

--- 输入参数 ---
脏页速率: %.1f MB/s
总内存: %.1f MB
网络带宽: %.1f MB/s

--- 推荐方案 ---
模式: %s
预拷贝迭代数: %d
预估总时间: %.1f 秒
预估服务中断: %.0f 毫秒
预估成功率: %.1f%%

--- 详细分析 ---
`, r.ContainerName, r.Timestamp.Format("2006-01-02 15:04:05"),
		r.DirtyRate/(1024*1024), float64(r.TotalMemory)/(1024*1024),
		r.NetworkBandwidth/(1024*1024),
		r.RecommendedMode, r.PreCopyIters,
		r.EstimatedTotalTime, r.EstimatedDowntime,
		r.SuccessProbability*100)

	if r.PreCopyAnalysis != nil {
		text += fmt.Sprintf(`
预拷贝模式:
  可行: %v
  最优迭代: %d
  总传输量: %.1f MB
  服务中断: %.0f ms
  原因: %s
`, r.PreCopyAnalysis.Feasible, r.PreCopyAnalysis.OptimalIterations,
			r.PreCopyAnalysis.TotalTransferMB, r.PreCopyAnalysis.DowntimeMs,
			r.PreCopyAnalysis.Reason)
	}

	if r.PostCopyAnalysis != nil {
		text += fmt.Sprintf(`
后拷贝模式:
  可行: %v
  缺页率: %.1f 页/秒
  恢复时间: %.0f ms
  原因: %s
`, r.PostCopyAnalysis.Feasible, r.PostCopyAnalysis.PageFaultRate,
			r.PostCopyAnalysis.RecoveryTimeMs, r.PostCopyAnalysis.Reason)
	}

	if r.DirectAnalysis != nil {
		text += fmt.Sprintf(`
直接迁移:
  可行: %v
  传输量: %.1f MB
  服务中断: %.0f ms
  原因: %s
`, r.DirectAnalysis.Feasible, r.DirectAnalysis.TransferMB,
			r.DirectAnalysis.DowntimeMs, r.DirectAnalysis.Reason)
	}

	text += fmt.Sprintf(`
模型版本: %s
训练样本: %d
`, r.ModelVersion, r.TrainingSamples)

	return text
}

func (r *PredictionResult) ToJSON() ([]byte, error) {
	return json.MarshalIndent(r, "", "  ")
}
