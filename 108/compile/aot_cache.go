package compile

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"hash"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/bytecodealliance/wasmtime-go/v18"
	"go.uber.org/zap"
)

const (
	MaxCacheSizeGB     = 10
	MaxCacheSizeBytes  = MaxCacheSizeGB * 1024 * 1024 * 1024
	CacheFileExtension  = ".cache"
	MetadataExtension   = ".meta.json"
)

type OptimizationLevel int

const (
	OptLevel_O0 OptimizationLevel = iota
	OptLevel_O1
	OptLevel_O2
	OptLevel_O3
)

func (o OptimizationLevel) String() string {
	switch o {
	case OptLevel_O0:
		return "O0"
	case OptLevel_O1:
		return "O1"
	case OptLevel_O2:
		return "O2"
	case OptLevel_O3:
		return "O3"
	default:
		return "O2"
	}
}

type CPUFeature int

const (
	CPUFeature_SSE3    CPUFeature = 1 << iota
	CPUFeature_SSSE3
	CPUFeature_SSE41
	CPUFeature_SSE42
	CPUFeature_AVX
	CPUFeature_AVX2
	CPUFeature_AVX512
	CPUFeature_BMI1
	CPUFeature_BMI2
	CPUFeature_LZCNT
	CPUFeature_POPCNT
)

type CPUFeatures struct {
	Features    CPUFeature
	Brand       string
	Family      int
	Model       int
	Stepping    int
}

func (c *CPUFeatures) Hash() string {
	h := sha256.New()
	h.Write([]byte(c.Brand))
	h.Write([]byte{byte(c.Family)})
	h.Write([]byte{byte(c.Model)})
	h.Write([]byte{byte(c.Stepping)})
	binary.Write(h, binary.LittleEndian, uint64(c.Features))
	return hex.EncodeToString(h.Sum(nil))[:16]
}

type ModuleMetadata struct {
	Version         string            `json:"version"`
	ModuleHash      string            `json:"module_hash"`
	CompiledHash    string            `json:"compiled_hash"`
	CPUFeatures     string            `json:"cpu_features"`
	OptimizationLevel string         `json:"optimization_level"`
	CompiledAt      time.Time         `json:"compiled_at"`
	FileSize        int64             `json:"file_size"`
	CacheSize       int64             `json:"cache_size"`
	WasmFileMtime   int64             `json:"wasm_file_mtime"`
}

type CacheEntry struct {
	Metadata   ModuleMetadata
	CachePath  string
	LastAccess time.Time
	AccessCount int64
}

type AOTCacheManager struct {
	cacheDir    string
	entries     map[string]*CacheEntry
	mu          sync.RWMutex
	logger      *zap.Logger
	currentCPU  *CPUFeatures
	totalSize   int64
	maxSize     int64
	optLevel    OptimizationLevel
}

func NewAOTCacheManager(cacheDir string, optLevel OptimizationLevel, logger *zap.Logger) (*AOTCacheManager, error) {
	if err := os.MkdirAll(cacheDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create cache directory: %w", err)
	}

	cm := &AOTCacheManager{
		cacheDir:   cacheDir,
		entries:    make(map[string]*CacheEntry),
		logger:    logger,
		maxSize:   MaxCacheSizeBytes,
		optLevel:  optLevel,
	}

	cm.currentCPU = DetectCPUFeatures()

	if err := cm.loadMetadata(); err != nil {
		logger.Warn("failed to load existing cache metadata", zap.Error(err))
	}

	if err := cm.recalculateSize(); err != nil {
		logger.Warn("failed to recalculate cache size", zap.Error(err))
	}

	return cm, nil
}

func (m *AOTCacheManager) GetCacheKey(wasmPath string, version string) (string, error) {
	fileInfo, err := os.Stat(wasmPath)
	if err != nil {
		return "", fmt.Errorf("failed to stat wasm file: %w", err)
	}

	file, err := os.Open(wasmPath)
	if err != nil {
		return "", fmt.Errorf("failed to open wasm file: %w", err)
	}
	defer file.Close()

	hasher := sha256.New()
	hasher.Write([]byte(version))
	hasher.Write([]byte(fmt.Sprintf("%d", fileInfo.ModTime().Unix())))
	hasher.Write([]byte(fmt.Sprintf("%d", fileInfo.Size())))
	hasher.Write([]byte(m.currentCPU.Hash()))
	hasher.Write([]byte(m.optLevel.String()))

	_, err = io.Copy(hasher, file)
	if err != nil {
		return "", fmt.Errorf("failed to hash wasm file: %w", err)
	}

	return hex.EncodeToString(hasher.Sum(nil)), nil
}

func (m *AOTCacheManager) GetCached(wasmPath string, version string) ([]byte, *ModuleMetadata, bool, error) {
	cacheKey, err := m.GetCacheKey(wasmPath, version)
	if err != nil {
		return nil, nil, false, err
	}

	m.mu.RLock()
	entry, exists := m.entries[cacheKey]
	m.mu.RUnlock()

	if !exists {
		return nil, nil, false, nil
	}

	if err := m.validateEntry(entry, wasmPath); err != nil {
		m.logger.Warn("cache entry invalid", zap.String("cache_key", cacheKey), zap.Error(err))
		m.Remove(cacheKey)
		return nil, nil, false, nil
	}

	cacheData, err := os.ReadFile(entry.CachePath)
	if err != nil {
		return nil, nil, false, fmt.Errorf("failed to read cache file: %w", err)
	}

	m.mu.Lock()
	entry.LastAccess = time.Now()
	entry.AccessCount++
	m.mu.Unlock()

	m.logger.Info("cache hit", zap.String("cache_key", cacheKey), zap.Int64("size", int64(len(cacheData))))

	return cacheData, &entry.Metadata, true, nil
}

func (m *AOTCacheManager) Store(wasmPath string, version string, compiledData []byte, wasmFileMtime int64) error {
	cacheKey, err := m.GetCacheKey(wasmPath, version)
	if err != nil {
		return err
	}

	metadata := ModuleMetadata{
		Version:          version,
		ModuleHash:       cacheKey,
		CompiledHash:     m.computeDataHash(compiledData),
		CPUFeatures:      m.currentCPU.Hash(),
		OptimizationLevel: m.optLevel.String(),
		CompiledAt:       time.Now(),
		FileSize:         int64(len(compiledData)),
		CacheSize:        int64(len(compiledData)),
		WasmFileMtime:    wasmFileMtime,
	}

	cacheFileName := fmt.Sprintf("%s%s", cacheKey, CacheFileExtension)
	cachePath := filepath.Join(m.cacheDir, cacheFileName)

	if err := m.ensureSpace(len(compiledData)); err != nil {
		return fmt.Errorf("failed to ensure cache space: %w", err)
	}

	if err := os.WriteFile(cachePath, compiledData, 0644); err != nil {
		return fmt.Errorf("failed to write cache file: %w", err)
	}

	metaFileName := fmt.Sprintf("%s%s", cacheKey, MetadataExtension)
	metaPath := filepath.Join(m.cacheDir, metaFileName)

	metaData, err := json.MarshalIndent(metadata, "", "  ")
	if err != nil {
		os.Remove(cachePath)
		return fmt.Errorf("failed to marshal metadata: %w", err)
	}

	if err := os.WriteFile(metaPath, metaData, 0644); err != nil {
		os.Remove(cachePath)
		return fmt.Errorf("failed to write metadata file: %w", err)
	}

	entry := &CacheEntry{
		Metadata:   metadata,
		CachePath:  cachePath,
		LastAccess: time.Now(),
	}

	m.mu.Lock()
	m.entries[cacheKey] = entry
	m.totalSize += int64(len(compiledData))
	m.mu.Unlock()

	m.logger.Info("cached compiled module",
		zap.String("cache_key", cacheKey),
		zap.Int64("size", int64(len(compiledData))),
		zap.String("path", cachePath))

	return nil
}

func (m *AOTCacheManager) Remove(cacheKey string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	entry, exists := m.entries[cacheKey]
	if !exists {
		return nil
	}

	os.Remove(entry.CachePath)
	metaPath := strings.TrimSuffix(entry.CachePath, CacheFileExtension) + MetadataExtension
	os.Remove(metaPath)

	m.totalSize -= entry.Metadata.CacheSize
	delete(m.entries, cacheKey)

	m.logger.Info("removed cache entry", zap.String("cache_key", cacheKey))
	return nil
}

func (m *AOTCacheManager) InvalidateByWasmPath(wasmPath string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	var toRemove []string
	for key, entry := range m.entries {
		if strings.Contains(entry.CachePath, wasmPath) {
			toRemove = append(toRemove, key)
		}
	}

	for _, key := range toRemove {
		if entry, ok := m.entries[key]; ok {
			os.Remove(entry.CachePath)
			metaPath := strings.TrimSuffix(entry.CachePath, CacheFileExtension) + MetadataExtension
			os.Remove(metaPath)
			m.totalSize -= entry.Metadata.CacheSize
			delete(m.entries, key)
		}
	}

	return nil
}

func (m *AOTCacheManager) Clear() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	for key, entry := range m.entries {
		os.Remove(entry.CachePath)
		metaPath := strings.TrimSuffix(entry.CachePath, CacheFileExtension) + MetadataExtension
		os.Remove(metaPath)
		delete(m.entries, key)
	}

	m.totalSize = 0
	m.logger.Info("cleared all cache entries")
	return nil
}

func (m *AOTCacheManager) GetStats() (totalEntries int, totalSize int64, hits int64) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	var hits int64
	for _, entry := range m.entries {
		hits += entry.AccessCount
	}

	return len(m.entries), m.totalSize, hits
}

func (m *AOTCacheManager) loadMetadata() error {
	entries, err := os.ReadDir(m.cacheDir)
	if err != nil {
		return err
	}

	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}

		name := entry.Name()
		if !strings.HasSuffix(name, MetadataExtension) {
			continue
		}

		metaPath := filepath.Join(m.cacheDir, name)
		metaData, err := os.ReadFile(metaPath)
		if err != nil {
			continue
		}

		var metadata ModuleMetadata
		if err := json.Unmarshal(metaData, &metadata); err != nil {
			continue
		}

		cacheKey := strings.TrimSuffix(name, MetadataExtension)
		cachePath := strings.TrimSuffix(metaPath, MetadataExtension) + CacheFileExtension

		if _, err := os.Stat(cachePath); os.IsNotExist(err) {
			os.Remove(metaPath)
			continue
		}

		m.entries[cacheKey] = &CacheEntry{
			Metadata:  metadata,
			CachePath: cachePath,
		}
		m.totalSize += metadata.CacheSize
	}

	return nil
}

func (m *AOTCacheManager) recalculateSize() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	var total int64
	for _, entry := range m.entries {
		info, err := os.Stat(entry.CachePath)
		if err != nil {
			continue
		}
		entry.Metadata.CacheSize = info.Size()
		total += info.Size()
	}
	m.totalSize = total

	return nil
}

func (m *AOTCacheManager) ensureSpace(needed int) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.totalSize+int64(needed) <= m.maxSize {
		return nil
	}

	evicted := m.totalSize + int64(needed) - m.maxSize
	if err := m.evictLRU(evicted); err != nil {
		return err
	}

	return nil
}

func (m *AOTCacheManager) evictLRU(targetBytes int64) error {
	type entryWithTime struct {
		key       string
		entry     *CacheEntry
		accessedAt time.Time
	}

	var entries []entryWithTime
	for key, entry := range m.entries {
		entries = append(entries, entryWithTime{
			key:       key,
			entry:     entry,
			accessedAt: entry.LastAccess,
		})
	}

	sort.Slice(entries, func(i, j int) bool {
		return entries[i].accessedAt.Before(entries[j].accessedAt)
	})

	var freed int64
	for _, e := range entries {
		if freed >= targetBytes {
			break
		}

		os.Remove(e.entry.CachePath)
		metaPath := strings.TrimSuffix(e.entry.CachePath, CacheFileExtension) + MetadataExtension
		os.Remove(metaPath)

		freed += e.entry.Metadata.CacheSize
		m.totalSize -= e.entry.Metadata.CacheSize
		delete(m.entries, e.key)

		m.logger.Info("evicted cache entry",
			zap.String("cache_key", e.key),
			zap.Int64("freed_bytes", freed))
	}

	return nil
}

func (m *AOTCacheManager) validateEntry(entry *CacheEntry, wasmPath string) error {
	if _, err := os.Stat(wasmPath); os.IsNotExist(err) {
		return fmt.Errorf("wasm file no longer exists")
	}

	fileInfo, err := os.Stat(wasmPath)
	if err != nil {
		return err
	}

	if fileInfo.ModTime().Unix() != entry.Metadata.WasmFileMtime {
		return fmt.Errorf("wasm file has been modified")
	}

	if entry.Metadata.CompiledHash != m.currentCPU.Hash() {
		return fmt.Errorf("cpu features mismatch")
	}

	return nil
}

func (m *AOTCacheManager) computeDataHash(data []byte) string {
	h := sha256.New()
	h.Write(data)
	return hex.EncodeToString(h.Sum(nil))[:16]
}

func (m *AOTCacheManager) CompileWasm(wasmPath string, version string) error {
	wasmData, err := os.ReadFile(wasmPath)
	if err != nil {
		return fmt.Errorf("failed to read wasm file: %w", err)
	}

	fileInfo, err := os.Stat(wasmPath)
	if err != nil {
		return fmt.Errorf("failed to stat wasm file: %w", err)
	}

	engineConfig := wasmtime.NewConfig()

	engine := wasmtime.NewEngineWithConfig(engineConfig)
	store := wasmtime.NewStore(engine)

	module, err := wasmtime.NewModule(store, wasmData)
	if err != nil {
		return fmt.Errorf("failed to compile wasm module: %w", err)
	}

	cacheData, err := engine.SerializeModule(module)
	if err != nil {
		return fmt.Errorf("failed to serialize wasm module: %w", err)
	}

	return m.Store(wasmPath, version, cacheData, fileInfo.ModTime().Unix())
}

func (m *AOTCacheManager) PrecompileAll(wasmFiles map[string]string) error {
	for wasmPath, version := range wasmFiles {
		m.logger.Info("precompiling wasm module",
			zap.String("path", wasmPath),
			zap.String("version", version))

		if err := m.CompileWasm(wasmPath, version); err != nil {
			m.logger.Error("failed to precompile",
				zap.String("path", wasmPath),
				zap.Error(err))
			return err
		}
	}

	return nil
}
