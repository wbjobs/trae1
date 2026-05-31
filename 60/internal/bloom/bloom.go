package bloom

import (
	"encoding/binary"
	"hash/fnv"
	"math"
	"sync"
	"sync/atomic"
	"time"

	"cdn-cache-protector/internal/model"
)

type singleFilter struct {
	bitSet    []uint64
	size      uint64
	hashNum   uint64
	itemCount atomic.Int64
}

func newSingleFilter(expectedItems uint64, falsePositiveRate float64) *singleFilter {
	size := optimalSize(expectedItems, falsePositiveRate)
	hashNum := optimalHashNum(size, expectedItems)
	size = (size + 63) / 64 * 64

	return &singleFilter{
		bitSet:  make([]uint64, size/64),
		size:    size,
		hashNum: hashNum,
	}
}

func (sf *singleFilter) hash(data []byte, seed uint64) uint64 {
	h := fnv.New64a()
	seedBytes := make([]byte, 8)
	binary.LittleEndian.PutUint64(seedBytes, seed)
	h.Write(seedBytes)
	h.Write(data)
	return h.Sum64()
}

func (sf *singleFilter) add(key string) {
	data := []byte(key)
	for i := uint64(0); i < sf.hashNum; i++ {
		hash := sf.hash(data, i) % sf.size
		sf.bitSet[hash/64] |= 1 << (hash % 64)
	}
	sf.itemCount.Add(1)
}

func (sf *singleFilter) contains(key string) bool {
	data := []byte(key)
	for i := uint64(0); i < sf.hashNum; i++ {
		hash := sf.hash(data, i) % sf.size
		if sf.bitSet[hash/64]&(1<<(hash%64)) == 0 {
			return false
		}
	}
	return true
}

func (sf *singleFilter) itemCountVal() int64 {
	return sf.itemCount.Load()
}

func (sf *singleFilter) memorySize() int64 {
	return int64(len(sf.bitSet) * 8)
}

type AdaptiveFilter struct {
	current       atomic.Pointer[singleFilter]
	rebuilding    atomic.Bool
	rebuildMu     sync.Mutex
	targetFPR     float64
	expectedItems uint64

	totalRequests  atomic.Int64
	bloomHits      atomic.Int64
	bloomMisses    atomic.Int64
	falsePositives atomic.Int64

	rebuildCount   atomic.Int64
	lastRebuildTime atomic.Value

	keysMu sync.RWMutex
	keys   map[string]struct{}
}

func New(expectedItems uint64, targetFPR float64) *AdaptiveFilter {
	if targetFPR <= 0 || targetFPR > 0.05 {
		targetFPR = 0.01
	}

	af := &AdaptiveFilter{
		targetFPR:     targetFPR,
		expectedItems: expectedItems,
		keys:          make(map[string]struct{}),
	}

	sf := newSingleFilter(expectedItems, targetFPR)
	af.current.Store(sf)
	af.lastRebuildTime.Store(time.Now())

	go af.monitor()

	return af
}

func optimalSize(n uint64, p float64) uint64 {
	if p <= 0 || p >= 1 {
		p = 0.01
	}
	m := -float64(n) * math.Log(p) / (math.Ln2 * math.Ln2)
	return uint64(math.Ceil(m))
}

func optimalHashNum(m, n uint64) uint64 {
	if n == 0 {
		return 1
	}
	k := float64(m) / float64(n) * math.Ln2
	result := uint64(math.Ceil(k))
	if result < 1 {
		result = 1
	}
	return result
}

func (af *AdaptiveFilter) Add(key string) {
	af.keysMu.Lock()
	af.keys[key] = struct{}{}
	af.keysMu.Unlock()

	current := af.current.Load()
	current.add(key)
}

func (af *AdaptiveFilter) Contains(key string) bool {
	current := af.current.Load()
	return current.contains(key)
}

func (af *AdaptiveFilter) RecordRequest(key string, exists bool) {
	af.totalRequests.Add(1)
	if exists {
		af.bloomHits.Add(1)
	} else {
		af.bloomMisses.Add(1)
	}
}

func (af *AdaptiveFilter) RecordFalsePositive() {
	af.falsePositives.Add(1)
}

func (af *AdaptiveFilter) GetStats() model.BloomFilterStats {
	totalRequests := af.totalRequests.Load()
	bloomHits := af.bloomHits.Load()
	bloomMisses := af.bloomMisses.Load()
	falsePositives := af.falsePositives.Load()

	current := af.current.Load()

	falsePositiveRate := float64(0)
	if bloomHits > 0 {
		falsePositiveRate = float64(falsePositives) / float64(bloomHits)
	}

	return model.BloomFilterStats{
		TotalRequests:    totalRequests,
		BloomHits:        bloomHits,
		BloomMisses:      bloomMisses,
		FalsePositives:   falsePositives,
		FalsePositiveRate: falsePositiveRate,
		EstimatedSize:    current.itemCountVal(),
	}
}

func (af *AdaptiveFilter) GetAdaptiveStats() map[string]interface{} {
	current := af.current.Load()
	lastRebuild, _ := af.lastRebuildTime.Load().(time.Time)

	return map[string]interface{}{
		"target_fpr":          af.targetFPR,
		"current_fpr":         af.currentFPR(),
		"expected_items":      af.expectedItems,
		"current_items":       current.itemCountVal(),
		"bf_size_bytes":       current.memorySize(),
		"bf_hash_functions":   current.hashNum,
		"is_rebuilding":       af.rebuilding.Load(),
		"rebuild_count":       af.rebuildCount.Load(),
		"last_rebuild_time":   lastRebuild.Format(time.RFC3339),
		"tracked_keys":        len(af.keys),
	}
}

func (af *AdaptiveFilter) currentFPR() float64 {
	bloomHits := af.bloomHits.Load()
	falsePositives := af.falsePositives.Load()

	if bloomHits == 0 {
		return 0
	}
	return float64(falsePositives) / float64(bloomHits)
}

func (af *AdaptiveFilter) monitor() {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for range ticker.C {
		if af.needsRebuild() {
			af.Rebuild()
		}
	}
}

func (af *AdaptiveFilter) needsRebuild() bool {
	if af.rebuilding.Load() {
		return false
	}

	currentFPR := af.currentFPR()
	return currentFPR > af.targetFPR && af.bloomHits.Load() > 100
}

func (af *AdaptiveFilter) Rebuild() error {
	if !af.rebuilding.CompareAndSwap(false, true) {
		return nil
	}
	defer af.rebuilding.Store(false)

	af.rebuildMu.Lock()
	defer af.rebuildMu.Unlock()

	current := af.current.Load()
	currentItemCount := current.itemCountVal()

	newExpectedItems := af.expectedItems
	if currentItemCount > int64(newExpectedItems)*8/10 {
		newExpectedItems = uint64(float64(currentItemCount) * 2)
	}

	if newExpectedItems < af.expectedItems {
		newExpectedItems = af.expectedItems
	}

	newFilter := newSingleFilter(newExpectedItems, af.targetFPR)

	af.keysMu.RLock()
	for key := range af.keys {
		newFilter.add(key)
	}
	af.keysMu.RUnlock()

	af.current.Store(newFilter)

	af.rebuildCount.Add(1)
	af.lastRebuildTime.Store(time.Now())

	return nil
}

func (af *AdaptiveFilter) Reset() {
	af.keysMu.Lock()
	af.keys = make(map[string]struct{})
	af.keysMu.Unlock()

	sf := newSingleFilter(af.expectedItems, af.targetFPR)
	af.current.Store(sf)

	af.totalRequests.Store(0)
	af.bloomHits.Store(0)
	af.bloomMisses.Store(0)
	af.falsePositives.Store(0)
	af.lastRebuildTime.Store(time.Now())
}

func (af *AdaptiveFilter) ItemCount() int64 {
	current := af.current.Load()
	return current.itemCountVal()
}

func (af *AdaptiveFilter) TargetFPR() float64 {
	return af.targetFPR
}

func (af *AdaptiveFilter) UpdateTargetFPR(fpr float64) {
	if fpr <= 0.001 || fpr > 0.05 {
		return
	}
	af.targetFPR = fpr
}

type Filter = AdaptiveFilter
