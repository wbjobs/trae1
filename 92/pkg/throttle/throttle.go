package throttle

import (
	"fmt"
	"sync"
	"time"

	"github.com/io-qos/io-qos/internal/types"
	"github.com/io-qos/io-qos/pkg/cgroup"
	"github.com/io-qos/io-qos/pkg/container"
	"github.com/io-qos/io-qos/pkg/logger"
)

type ThrottleState int

const (
	StateNormal ThrottleState = iota
	StateThrottled
	StateRecovering
)

type ContainerThrottleState struct {
	ContainerID      string
	ContainerName    string
	CgroupPath       string
	OriginalBPS      int64
	OriginalIOPS     int64
	CurrentBPS       int64
	CurrentIOPS      int64
	MinBPS           int64
	MinIOPS          int64
	State            ThrottleState
	ThrottledAt      time.Time
	LastAdjustTime   time.Time
	ReduceRatio      float64
}

type ThrottleManager struct {
	mu              sync.Mutex
	containers      map[string]*ContainerThrottleState
	ioCtl           *cgroup.IOController
	latencyMonitor  *types.LatencyMonitor
	cgroupRoot      string
	thresholdMs     float64
	thresholdDur    time.Duration
	recoveryDur     time.Duration
	reduceRatio     float64
	baseMinBPS      int64
	baseMinIOPS     int64
	stopChan        chan struct{}
	running         bool
}

func NewThrottleManager(cgroupRoot string, thresholdMs float64, thresholdDur time.Duration) *ThrottleManager {
	return &ThrottleManager{
		containers:  make(map[string]*ContainerThrottleState),
		ioCtl:       cgroup.NewIOControllerWithRoot(cgroupRoot),
		cgroupRoot:  cgroupRoot,
		thresholdMs: thresholdMs,
		thresholdDur: thresholdDur,
		recoveryDur: 30 * time.Second,
		reduceRatio: 0.5,
		baseMinBPS:  1 * 1024 * 1024,
		baseMinIOPS: 10,
		stopChan:    make(chan struct{}),
	}
}

func (tm *ThrottleManager) SetLatencyMonitor(lm *types.LatencyMonitor) {
	tm.mu.Lock()
	defer tm.mu.Unlock()
	tm.latencyMonitor = lm
}

func (tm *ThrottleManager) SetReductionRatio(ratio float64) {
	tm.mu.Lock()
	defer tm.mu.Unlock()
	tm.reduceRatio = ratio
}

func (tm *ThrottleManager) SetMinBandwidth(bps int64, iops int64) {
	tm.mu.Lock()
	defer tm.mu.Unlock()
	tm.baseMinBPS = bps
	tm.baseMinIOPS = iops
}

func (tm *ThrottleManager) RegisterContainer(info container.ContainerInfo, priority string) {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	weight := types.GetPriorityWeight(priority)
	minBPS := tm.baseMinBPS
	minIOPS := tm.baseMinIOPS

	switch priority {
	case types.PriorityHigh:
		minBPS = tm.baseMinBPS * 5
		minIOPS = tm.baseMinIOPS * 10
	case types.PriorityMedium:
		minBPS = tm.baseMinBPS * 2
		minIOPS = tm.baseMinIOPS * 3
	}

	tm.containers[info.ID] = &ContainerThrottleState{
		ContainerID:   info.ID,
		ContainerName: info.Name,
		CgroupPath:    info.CgroupPath,
		OriginalBPS:   0,
		OriginalIOPS:  0,
		CurrentBPS:    0,
		CurrentIOPS:   0,
		MinBPS:        minBPS,
		MinIOPS:       minIOPS,
		State:         StateNormal,
		ReduceRatio:   1.0,
	}

	logger.Info("[Throttle] Registered container: %s (priority=%s, min_bps=%d, min_iops=%d, weight=%d)",
		info.Name, priority, minBPS, minIOPS, weight)
}

func (tm *ThrottleManager) UnregisterContainer(containerID string) {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	if state, exists := tm.containers[containerID]; exists {
		if state.State == StateThrottled {
			tm.restoreLimits(state)
		}
		delete(tm.containers, containerID)
		logger.Info("[Throttle] Unregistered container: %s", containerID)
	}
}

func (tm *ThrottleManager) CheckAndThrottle(currentLatency float64) {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	highLatency := currentLatency > tm.thresholdMs

	for _, state := range tm.containers {
		switch state.State {
		case StateNormal:
			if highLatency {
				tm.throttleContainer(state)
			}

		case StateThrottled:
			if !highLatency && time.Since(state.ThrottledAt) > tm.recoveryDur {
				tm.restoreContainer(state)
			} else if time.Since(state.LastAdjustTime) > 10*time.Second {
				tm.adjustThrottle(state, currentLatency)
			}

		case StateRecovering:
			if !highLatency {
				elapsed := time.Since(state.ThrottledAt)
				if elapsed > tm.recoveryDur {
					state.State = StateNormal
					state.ReduceRatio = 1.0
					logger.Info("[Throttle] Container %s recovered to normal", state.ContainerID)
				}
			} else {
				tm.throttleContainer(state)
			}
		}
	}
}

func (tm *ThrottleManager) throttleContainer(state *ContainerThrottleState) {
	if state.State == StateThrottled {
		return
	}

	currentLimits, err := tm.ioCtl.GetCurrentLimits(state.CgroupPath)
	if err != nil {
		logger.Error("[Throttle] Failed to get current limits for %s: %v", state.ContainerID, err)
		return
	}

	state.OriginalBPS = currentLimits.WriteBPS
	if currentLimits.ReadBPS > state.OriginalBPS {
		state.OriginalBPS = currentLimits.ReadBPS
	}
	state.OriginalIOPS = currentLimits.WriteIOPS
	if currentLimits.ReadIOPS > state.OriginalIOPS {
		state.OriginalIOPS = currentLimits.ReadIOPS
	}

	if state.OriginalBPS == 0 {
		state.OriginalBPS = 100 * 1024 * 1024
	}
	if state.OriginalIOPS == 0 {
		state.OriginalIOPS = 1000
	}

	ratio := tm.reduceRatio
	originalWeight := types.GetPriorityWeight(state.getPriority())
	if originalWeight >= 80 {
		ratio = 0.7
	} else if originalWeight >= 30 {
		ratio = 0.5
	} else {
		ratio = 0.3
	}

	newBPS := int64(float64(state.OriginalBPS) * ratio)
	newIOPS := int64(float64(state.OriginalIOPS) * ratio)

	if newBPS < state.MinBPS {
		newBPS = state.MinBPS
	}
	if newIOPS < state.MinIOPS {
		newIOPS = state.MinIOPS
	}

	devices, err := tm.ioCtl.GetBlockDevices()
	if err != nil {
		logger.Error("[Throttle] Failed to get block devices: %v", err)
		return
	}

	newLimits := types.IOLimit{
		ReadBPS:   newBPS,
		WriteBPS:  newBPS,
		ReadIOPS:  newIOPS,
		WriteIOPS: newIOPS,
	}

	for _, dev := range devices {
		if err := tm.ioCtl.SetIOLimit(state.CgroupPath, dev, newLimits); err != nil {
			logger.Error("[Throttle] Failed to apply throttle to %s: %v", state.ContainerID, err)
			return
		}
	}

	state.State = StateThrottled
	state.CurrentBPS = newBPS
	state.CurrentIOPS = newIOPS
	state.ThrottledAt = time.Now()
	state.LastAdjustTime = time.Now()
	state.ReduceRatio = ratio

	logger.Event("throttle_apply", map[string]interface{}{
		"container_id":    state.ContainerID,
		"container_name":  state.ContainerName,
		"original_bps":    state.OriginalBPS,
		"original_iops":   state.OriginalIOPS,
		"reduced_bps":     newBPS,
		"reduced_iops":    newIOPS,
		"reduction_ratio": ratio,
		"throttled_at":    state.ThrottledAt.Format(time.RFC3339),
	})

	logger.Warn("[Throttle] Throttled container %s: BPS %d->%d, IOPS %d->%d (ratio=%.2f)",
		state.ContainerName, state.OriginalBPS, newBPS, state.OriginalIOPS, newIOPS, ratio)
}

func (tm *ThrottleManager) restoreContainer(state *ContainerThrottleState) {
	devices, err := tm.ioCtl.GetBlockDevices()
	if err != nil {
		logger.Error("[Throttle] Failed to get block devices: %v", err)
		return
	}

	originalLimits := types.IOLimit{
		ReadBPS:   state.OriginalBPS,
		WriteBPS:  state.OriginalBPS,
		ReadIOPS:  state.OriginalIOPS,
		WriteIOPS: state.OriginalIOPS,
	}

	for _, dev := range devices {
		if err := tm.ioCtl.SetIOLimit(state.CgroupPath, dev, originalLimits); err != nil {
			logger.Error("[Throttle] Failed to restore limits for %s: %v", state.ContainerID, err)
			return
		}
	}

	throttleDuration := time.Since(state.ThrottledAt)
	state.State = StateRecovering
	state.CurrentBPS = state.OriginalBPS
	state.CurrentIOPS = state.OriginalIOPS

	logger.Event("throttle_restore", map[string]interface{}{
		"container_id":       state.ContainerID,
		"container_name":     state.ContainerName,
		"restored_bps":       state.OriginalBPS,
		"restored_iops":      state.OriginalIOPS,
		"throttle_duration":  throttleDuration.Seconds(),
		"restored_at":        time.Now().Format(time.RFC3339),
	})

	logger.Info("[Throttle] Restored container %s: BPS=%d, IOPS=%d (throttled for %.2fs)",
		state.ContainerName, state.OriginalBPS, state.OriginalIOPS, throttleDuration.Seconds())
}

func (tm *ThrottleManager) adjustThrottle(state *ContainerThrottleState, currentLatency float64) {
	excessRatio := (currentLatency - tm.thresholdMs) / tm.thresholdMs
	if excessRatio < 0 {
		excessRatio = 0
	}

	adjustRatio := 1.0 - (excessRatio * 0.1)
	if adjustRatio < 0.3 {
		adjustRatio = 0.3
	}

	newBPS := int64(float64(state.OriginalBPS) * adjustRatio)
	newIOPS := int64(float64(state.OriginalIOPS) * adjustRatio)

	if newBPS < state.MinBPS {
		newBPS = state.MinBPS
	}
	if newIOPS < state.MinIOPS {
		newIOPS = state.MinIOPS
	}

	devices, _ := tm.ioCtl.GetBlockDevices()
	newLimits := types.IOLimit{
		ReadBPS:   newBPS,
		WriteBPS:  newBPS,
		ReadIOPS:  newIOPS,
		WriteIOPS: newIOPS,
	}

	for _, dev := range devices {
		tm.ioCtl.SetIOLimit(state.CgroupPath, dev, newLimits)
	}

	oldBPS := state.CurrentBPS
	state.CurrentBPS = newBPS
	state.CurrentIOPS = newIOPS
	state.ReduceRatio = adjustRatio
	state.LastAdjustTime = time.Now()

	logger.Debug("[Throttle] Adjusted container %s: BPS %d->%d, IOPS %d->%d",
		state.ContainerName, oldBPS, newBPS, state.OriginalIOPS, newIOPS)
}

func (tm *ThrottleManager) restoreLimits(state *ContainerThrottleState) {
	tm.restoreContainer(state)
}

func (tm *ThrottleManager) GetState(containerID string) *ContainerThrottleState {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	if state, exists := tm.containers[containerID]; exists {
		copyState := *state
		return &copyState
	}
	return nil
}

func (tm *ThrottleManager) GetAllStates() []ContainerThrottleState {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	var states []ContainerThrottleState
	for _, state := range tm.containers {
		states = append(states, *state)
	}
	return states
}

func (tm *ThrottleManager) GetThrottledContainers() []ContainerThrottleState {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	var throttled []ContainerThrottleState
	for _, state := range tm.containers {
		if state.State == StateThrottled {
			throttled = append(throttled, *state)
		}
	}
	return throttled
}

func (tm *ThrottleManager) ManualThrottle(containerID string, ratio float64) error {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	state, exists := tm.containers[containerID]
	if !exists {
		return fmt.Errorf("container %s not registered", containerID)
	}

	if state.OriginalBPS == 0 {
		currentLimits, err := tm.ioCtl.GetCurrentLimits(state.CgroupPath)
		if err == nil {
			state.OriginalBPS = currentLimits.WriteBPS
			state.OriginalIOPS = currentLimits.WriteIOPS
		}
		if state.OriginalBPS == 0 {
			state.OriginalBPS = 100 * 1024 * 1024
		}
		if state.OriginalIOPS == 0 {
			state.OriginalIOPS = 1000
		}
	}

	newBPS := int64(float64(state.OriginalBPS) * ratio)
	newIOPS := int64(float64(state.OriginalIOPS) * ratio)

	if newBPS < state.MinBPS {
		newBPS = state.MinBPS
	}
	if newIOPS < state.MinIOPS {
		newIOPS = state.MinIOPS
	}

	devices, _ := tm.ioCtl.GetBlockDevices()
	newLimits := types.IOLimit{
		ReadBPS:   newBPS,
		WriteBPS:  newBPS,
		ReadIOPS:  newIOPS,
		WriteIOPS: newIOPS,
	}

	for _, dev := range devices {
		if err := tm.ioCtl.SetIOLimit(state.CgroupPath, dev, newLimits); err != nil {
			return err
		}
	}

	oldRatio := state.ReduceRatio
	state.State = StateThrottled
	state.CurrentBPS = newBPS
	state.CurrentIOPS = newIOPS
	state.ThrottledAt = time.Now()
	state.LastAdjustTime = time.Now()
	state.ReduceRatio = ratio

	logger.Info("[Throttle] Manual throttle: %s (ratio %.2f->%.2f)", state.ContainerName, oldRatio, ratio)
	return nil
}

func (tm *ThrottleManager) ManualRestore(containerID string) error {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	state, exists := tm.containers[containerID]
	if !exists {
		return fmt.Errorf("container %s not registered", containerID)
	}

	if state.State == StateNormal {
		return fmt.Errorf("container %s not throttled", containerID)
	}

	tm.restoreContainer(state)
	return nil
}

func (tm *ThrottleManager) GetSummary() string {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	total := len(tm.containers)
	throttled := 0

	for _, state := range tm.containers {
		if state.State == StateThrottled {
			throttled++
		}
	}

	return fmt.Sprintf("Total: %d, Throttled: %d, Threshold: %.0fms/%v",
		total, throttled, tm.thresholdMs, tm.thresholdDur)
}

func (s *ContainerThrottleState) getPriority() string {
	ratio := s.ReduceRatio
	if ratio >= 0.7 {
		return types.PriorityHigh
	} else if ratio >= 0.4 {
		return types.PriorityMedium
	}
	return types.PriorityLow
}
