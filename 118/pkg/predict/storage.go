package predict

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"
)

type MigrationRecord struct {
	ID              string        `json:"id"`
	ContainerName   string        `json:"container_name"`
	Timestamp       time.Time     `json:"timestamp"`
	SourceHost      string        `json:"source_host"`
	TargetHost      string        `json:"target_host"`

	Mode            MigrationMode `json:"mode"`
	PreCopyIters    int           `json:"precopy_iters"`

	DirtyRate       float64       `json:"dirty_rate_bytes_per_sec"`
	TotalMemory     int64         `json:"total_memory_bytes"`
	NetworkBW       float64       `json:"network_bandwidth_bytes_per_sec"`
	HasGPU          bool          `json:"has_gpu"`

	ActualTotalTime float64       `json:"actual_total_time_sec"`
	ActualDowntime  float64       `json:"actual_downtime_ms"`
	Success         bool          `json:"success"`
	Error           string        `json:"error,omitempty"`

	PredictedTime   float64       `json:"predicted_time_sec"`
	PredictedDown   float64       `json:"predicted_downtime_ms"`

	DataTransferred int64         `json:"data_transferred_bytes"`
}

type HistoryStore struct {
	mu        sync.RWMutex
	records   []MigrationRecord
	filePath  string
	maxRecords int
}

func NewHistoryStore(dataDir string) *HistoryStore {
	return &HistoryStore{
		records:    make([]MigrationRecord, 0),
		filePath:   filepath.Join(dataDir, "migration_history.json"),
		maxRecords: 1000,
	}
}

func (h *HistoryStore) Load() error {
	h.mu.Lock()
	defer h.mu.Unlock()

	data, err := os.ReadFile(h.filePath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return fmt.Errorf("读取历史数据失败: %w", err)
	}

	if len(data) == 0 {
		return nil
	}

	if err := json.Unmarshal(data, &h.records); err != nil {
		return fmt.Errorf("解析历史数据失败: %w", err)
	}

	return nil
}

func (h *HistoryStore) Save() error {
	h.mu.RLock()
	defer h.mu.RUnlock()

	dir := filepath.Dir(h.filePath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("创建历史目录失败: %w", err)
	}

	data, err := json.MarshalIndent(h.records, "", "  ")
	if err != nil {
		return fmt.Errorf("序列化历史数据失败: %w", err)
	}

	return os.WriteFile(h.filePath, data, 0644)
}

func (h *HistoryStore) AddRecord(record MigrationRecord) {
	h.mu.Lock()
	defer h.mu.Unlock()

	h.records = append(h.records, record)

	if len(h.records) > h.maxRecords {
		h.records = h.records[len(h.records)-h.maxRecords:]
	}
}

func (h *HistoryStore) GetRecords(containerName string, limit int) []MigrationRecord {
	h.mu.RLock()
	defer h.mu.RUnlock()

	var results []MigrationRecord
	count := 0

	for i := len(h.records) - 1; i >= 0 && count < limit; i-- {
		if containerName == "" || h.records[i].ContainerName == containerName {
			results = append(results, h.records[i])
			count++
		}
	}

	return results
}

func (h *HistoryStore) GetAllRecords() []MigrationRecord {
	h.mu.RLock()
	defer h.mu.RUnlock()

	result := make([]MigrationRecord, len(h.records))
	copy(result, h.records)
	return result
}

func (h *HistoryStore) Stats() (total, success, failed int, avgTime, avgDowntime float64) {
	h.mu.RLock()
	defer h.mu.RUnlock()

	total = len(h.records)
	var totalTime, totalDowntime float64

	for _, r := range h.records {
		if r.Success {
			success++
			totalTime += r.ActualTotalTime
			totalDowntime += r.ActualDowntime
		} else {
			failed++
		}
	}

	if success > 0 {
		avgTime = totalTime / float64(success)
		avgDowntime = totalDowntime / float64(success)
	}

	return
}

type FeedbackCollector struct {
	store     *HistoryStore
	predictor *Predictor
}

func NewFeedbackCollector(dataDir string, predictor *Predictor) *FeedbackCollector {
	store := NewHistoryStore(dataDir)
	store.Load()

	return &FeedbackCollector{
		store:     store,
		predictor: predictor,
	}
}

func (f *FeedbackCollector) RecordFeedback(record MigrationRecord) error {
	f.store.AddRecord(record)
	if err := f.store.Save(); err != nil {
		return fmt.Errorf("保存历史记录失败: %w", err)
	}

	f.trainFromRecord(record)
	return nil
}

func (f *FeedbackCollector) trainFromRecord(record MigrationRecord) {
	learningRate := 0.01

	dirtyRateMBs := record.DirtyRate / (1024 * 1024)
	bandwidthMBs := record.NetworkBW / (1024 * 1024)
	totalMemoryMB := float64(record.TotalMemory) / (1024 * 1024)

	dirtyRateRatio := 0.0
	if bandwidthMBs > 0 {
		dirtyRateRatio = dirtyRateMBs / bandwidthMBs
	}

	hasGPU := 0.0
	if record.HasGPU {
		hasGPU = 1.0
	}

	features := []float64{
		dirtyRateRatio,
		totalMemoryMB,
		bandwidthMBs,
		float64(record.PreCopyIters),
		hasGPU,
	}

	if record.Success {
		f.predictor.totalTimeModel.Update(features, record.ActualTotalTime, learningRate)
		f.predictor.downtimeModel.Update(features, record.ActualDowntime, learningRate)

		successFeatures := []float64{
			dirtyRateRatio,
			bandwidthMBs / 1000,
			1.0,
			0.5,
			0.9,
		}
		f.predictor.successModel.Update(successFeatures, 1.0, learningRate)
	} else {
		successFeatures := []float64{
			dirtyRateRatio,
			bandwidthMBs / 1000,
			0.5,
			0.8,
			0.5,
		}
		f.predictor.successModel.Update(successFeatures, 0.0, learningRate)
	}
}

func (f *FeedbackCollector) GetStore() *HistoryStore {
	return f.store
}

func (f *FeedbackCollector) GetPredictor() *Predictor {
	return f.predictor
}

func GenerateMigrationID() string {
	return fmt.Sprintf("mig-%d", time.Now().UnixNano())
}
