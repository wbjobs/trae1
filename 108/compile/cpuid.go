package compile

import (
	"fmt"
	"runtime"
	"strings"
)

func DetectCPUFeatures() *CPUFeatures {
	features := CPUFeature(0)
	brand := "unknown"
	family, model, stepping := 0, 0, 0

	switch runtime.GOOS {
	case "windows":
		features, brand, family, model, stepping = detectCPUWindows()
	case "linux":
		features, brand, family, model, stepping = detectCPULinux()
	case "darwin":
		features, brand, family, model, stepping = detectCPUMac()
	default:
		features = detectCPUGeneric()
	}

	return &CPUFeatures{
		Features: features,
		Brand:   brand,
		Family:  family,
		Model:   model,
		Stepping: stepping,
	}
}

func detectCPUWindows() (CPUFeature, string, int, int, int) {
	return CPUFeature(0), "generic", 0, 0, 0
}

func detectCPULinux() (CPUFeature, string, int, int, int) {
	return CPUFeature(0), "generic", 0, 0, 0
}

func detectCPUMac() (CPUFeature, string, int, int, int) {
	return CPUFeature(0), "generic", 0, 0, 0
}

func detectCPUGeneric() CPUFeature {
	features := CPUFeature(0)

	if supportsSSE3() {
		features |= CPUFeature_SSE3
	}
	if supportsSSSE3() {
		features |= CPUFeature_SSSE3
	}
	if supportsSSE41() {
		features |= CPUFeature_SSE41
	}
	if supportsSSE42() {
		features |= CPUFeature_SSE42
	}
	if supportsAVX() {
		features |= CPUFeature_AVX
	}
	if supportsAVX2() {
		features |= CPUFeature_AVX2
	}
	if supportsBMI1() {
		features |= CPUFeature_BMI1
	}
	if supportsBMI2() {
		features |= CPUFeature_BMI2
	}
	if supportsLZCNT() {
		features |= CPUFeature_LZCNT
	}
	if supportsPOPCNT() {
		features |= CPUFeature_POPCNT
	}

	return features
}

func supportsSSE3() bool {
	return true
}

func supportsSSSE3() bool {
	return true
}

func supportsSSE41() bool {
	return true
}

func supportsSSE42() bool {
	return true
}

func supportsAVX() bool {
	return true
}

func supportsAVX2() bool {
	return true
}

func supportsBMI1() bool {
	return true
}

func supportsBMI2() bool {
	return true
}

func supportsLZCNT() bool {
	return true
}

func supportsPOPCNT() bool {
	return true
}

func GetCPUCacheKey() string {
	cpu := DetectCPUFeatures()
	return cpu.Hash()
}

func IsCacheCompatible(cachedFeatures string) bool {
	current := DetectCPUFeatures()
	return cachedFeatures == current.Hash()
}

func GetCompilerSettings() map[string]interface{} {
	return map[string]interface{}{
		"cpu_features":   DetectCPUFeatures(),
		"go_version":     runtime.Version(),
		"goos":          runtime.GOOS,
		"goarch":         runtime.GOARCH,
		"num_cpu":        runtime.NumCPU(),
	}
}

type OptimizationSettings struct {
	Level           OptimizationLevel
	EnableVerifier  bool
	DebugInfo       bool
	MemoryLimitMB   int
	CraneliftFlags  map[string]bool
}

func GetOptimizationSettings(level OptimizationLevel) *OptimizationSettings {
	settings := &OptimizationSettings{
		Level:          level,
		EnableVerifier: level == OptLevel_O0,
		DebugInfo:      level == OptLevel_O0,
		MemoryLimitMB:  64,
		CraneliftFlags: make(map[string]bool),
	}

	switch level {
	case OptLevel_O0:
		settings.CraneliftFlags["enable_verifier"] = true
		settings.CraneliftFlags["debug_info"] = true
		settings.CraneliftFlags["opt_level"] = false
	case OptLevel_O1:
		settings.CraneliftFlags["enable_verifier"] = false
		settings.CraneliftFlags["opt_level"] = true
		settings.CraneliftFlags["speed"] = true
	case OptLevel_O2:
		settings.CraneliftFlags["enable_verifier"] = false
		settings.CraneliftFlags["opt_level"] = true
		settings.CraneliftFlags["speed"] = true
		settings.CraneliftFlags["shrink_size"] = true
	case OptLevel_O3:
		settings.CraneliftFlags["enable_verifier"] = false
		settings.CraneliftFlags["opt_level"] = true
		settings.CraneliftFlags["speed"] = true
		settings.CraneliftFlags["shrink_size"] = true
		settings.CraneliftFlags["enable_jump_hoisting"] = true
	}

	return settings
}

func GetHostCPUName() string {
	return runtime.GOARCH
}

func GetCompilerVersion() string {
	return "wasmtime-cranelift"
}

func VerifyCacheCompatibility(cacheMeta *ModuleMetadata) (bool, string) {
	if cacheMeta == nil {
		return false, "nil metadata"
	}

	currentCPU := DetectCPUFeatures()
	if cacheMeta.CPUFeatures != currentCPU.Hash() {
		return false, "cpu_features_mismatch"
	}

	if cacheMeta.OptimizationLevel != OptLevel_O2.String() {
		return false, "optimization_level_mismatch"
	}

	return true, ""
}

type CompilationProfile struct {
	ModulePath       string
	Version          string
	CompilationTime  int64
	CacheHit         bool
	CacheKey         string
	OptimizationLevel OptimizationLevel
	CPUFeatures      string
}

func (p *CompilationProfile) IsCacheCompatible() bool {
	if p == nil {
		return false
	}

	currentCPU := DetectCPUFeatures()
	if p.CPUFeatures != currentCPU.Hash() {
		return false
	}

	return true
}

func GetCacheStats() (entries int, size int64, maxSize int64) {
	return 0, 0, MaxCacheSizeBytes
}

func FormatCacheStats() string {
	entries, size, maxSize := GetCacheStats()
	return strings.TrimSpace(fmt.Sprintf("Cache: %d entries, %d / %d bytes (%.1f%% used)",
		entries, size, maxSize, float64(size)/float64(maxSize)*100))
}
