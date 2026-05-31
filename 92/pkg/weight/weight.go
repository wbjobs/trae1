package weight

import (
	"fmt"
	"sync"
	"time"

	"github.com/io-qos/io-qos/internal/types"
	"github.com/io-qos/io-qos/pkg/cgroup"
	"github.com/io-qos/io-qos/pkg/logger"
)

type WeightState int

const (
	StateNormal    WeightState = iota
	StateElevated              // 已提升
	StateRecovering            // 等待恢复
)

type ContainerWeightState struct {
	ContainerID      string
	ContainerName    string
	CgroupPath       string
	OriginalWeight   int
	CurrentWeight    int
	CurrentPriority  string
	OriginalPriority string
	State            WeightState
	ElevatedAt       time.Time
	LastCheckTime    time.Time
	LastBoostTime    time.Time
	BoostCount       int
	WaitStartTime    time.Time
	IsWaiting        bool
}

type WeightManager struct {
	mu             sync.Mutex
	states         map[string]*ContainerWeightState
	ioCtl          *cgroup.IOController
	starvationTime time.Duration
	cooldownTime   time.Duration
	minBoostTime   time.Duration
	cgroupRoot     string
}

func NewWeightManager(starvationTime time.Duration, cgroupRoot string) *WeightManager {
	return &WeightManager{
		states:         make(map[string]*ContainerWeightState),
		ioCtl:          cgroup.NewIOControllerWithRoot(cgroupRoot),
		starvationTime: starvationTime,
		cooldownTime:   5 * time.Second,
		minBoostTime:   10 * time.Second,
		cgroupRoot:     cgroupRoot,
	}
}

func (wm *WeightManager) SetStarvationTimeout(d time.Duration) {
	wm.mu.Lock()
	defer wm.mu.Unlock()
	wm.starvationTime = d
}

func (wm *WeightManager) GetStarvationTimeout() time.Duration {
	wm.mu.Lock()
	defer wm.mu.Unlock()
	return wm.starvationTime
}

func (wm *WeightManager) RegisterContainer(containerID, containerName, cgroupPath, priority string) {
	wm.mu.Lock()
	defer wm.mu.Unlock()

	weight := types.GetPriorityWeight(priority)
	wm.states[containerID] = &ContainerWeightState{
		ContainerID:      containerID,
		ContainerName:    containerName,
		CgroupPath:       cgroupPath,
		OriginalWeight:   weight,
		CurrentWeight:    weight,
		CurrentPriority:  priority,
		OriginalPriority: priority,
		State:            StateNormal,
		ElevatedAt:       time.Time{},
		LastCheckTime:    time.Now(),
		WaitStartTime:    time.Time{},
		IsWaiting:        false,
	}

	logger.Info("[WeightManager] Registered container: %s (weight=%d)", containerID, weight)
}

func (wm *WeightManager) UnregisterContainer(containerID string) {
	wm.mu.Lock()
	defer wm.mu.Unlock()

	if state, exists := wm.states[containerID]; exists {
		if state.State == StateElevated {
			wm.restoreWeight(state)
		}
		delete(wm.states, containerID)
		logger.Info("[WeightManager] Unregistered container: %s", containerID)
	}
}

func (wm *WeightManager) CheckAndAdjust(containerID string, waitTimeMs uint64, queueLength uint64) {
	wm.mu.Lock()
	defer wm.mu.Unlock()

	state, exists := wm.states[containerID]
	if !exists {
		return
	}

	state.LastCheckTime = time.Now()
	isStarving := waitTimeMs > uint64(wm.starvationTime.Milliseconds()) || queueLength > 0

	switch state.State {
	case StateNormal:
		if isStarving && state.OriginalWeight < types.GetPriorityWeight(types.PriorityMedium) {
			state.IsWaiting = true
			if state.WaitStartTime.IsZero() {
				state.WaitStartTime = time.Now()
			}

			waitDuration := time.Since(state.WaitStartTime)
			if waitDuration >= wm.starvationTime {
				wm.elevateWeight(state)
			}
		} else {
			state.IsWaiting = false
			state.WaitStartTime = time.Time{}
		}

	case StateElevated:
		elapsed := time.Since(state.ElevatedAt)
		if elapsed >= wm.minBoostTime && queueLength == 0 {
			wm.restoreWeight(state)
		}

	case StateRecovering:
		if !isStarving {
			elapsed := time.Since(state.ElevatedAt)
			if elapsed >= wm.cooldownTime {
				state.State = StateNormal
				logger.Debug("[WeightManager] Container %s recovered to normal state", state.ContainerID)
			}
		}
	}
}

func (wm *WeightManager) elevateWeight(state *ContainerWeightState) {
	if state.State != StateNormal {
		return
	}

	originalWeight := state.CurrentWeight
	elevatedWeight := types.GetPriorityWeight(types.PriorityMedium)

	devices, err := wm.ioCtl.GetBlockDevices()
	if err != nil {
		logger.Error("[WeightManager] Failed to get block devices for container %s: %v", state.ContainerID, err)
		return
	}

	for _, dev := range devices {
		if err := wm.ioCtl.SetIOPriority(state.CgroupPath, dev, types.PriorityMedium); err != nil {
			logger.Error("[WeightManager] Failed to elevate weight for container %s, device %s: %v",
				state.ContainerID, dev.Name, err)
			return
		}
	}

	state.State = StateElevated
	state.CurrentWeight = elevatedWeight
	state.CurrentPriority = types.PriorityMedium
	state.ElevatedAt = time.Now()
	state.LastBoostTime = time.Now()
	state.BoostCount++

	logger.Event("weight_elevate", map[string]interface{}{
		"container_id":      state.ContainerID,
		"container_name":    state.ContainerName,
		"original_weight":   originalWeight,
		"elevated_weight":   elevatedWeight,
		"elevated_at":       state.ElevatedAt.Format(time.RFC3339),
		"total_boosts":      state.BoostCount,
		"wait_duration_ms":  time.Since(state.WaitStartTime).Milliseconds(),
	})

	logger.Warn("[WeightManager] Starvation detected! Elevated container %s weight %d -> %d (boost #%d)",
		state.ContainerID, originalWeight, elevatedWeight, state.BoostCount)
}

func (wm *WeightManager) restoreWeight(state *ContainerWeightState) {
	devices, err := wm.ioCtl.GetBlockDevices()
	if err != nil {
		logger.Error("[WeightManager] Failed to get block devices for container %s: %v", state.ContainerID, err)
		return
	}

	for _, dev := range devices {
		if err := wm.ioCtl.SetIOPriority(state.CgroupPath, dev, state.OriginalPriority); err != nil {
			logger.Error("[WeightManager] Failed to restore weight for container %s, device %s: %v",
				state.ContainerID, dev.Name, err)
			return
		}
	}

	elevatedDuration := time.Since(state.ElevatedAt)
	state.State = StateRecovering
	state.CurrentWeight = state.OriginalWeight
	state.CurrentPriority = state.OriginalPriority
	state.IsWaiting = false
	state.WaitStartTime = time.Time{}

	logger.Event("weight_restore", map[string]interface{}{
		"container_id":         state.ContainerID,
		"container_name":       state.ContainerName,
		"restored_weight":      state.CurrentWeight,
		"elevated_duration":    elevatedDuration.Seconds(),
		"restored_at":          time.Now().Format(time.RFC3339),
	})

	logger.Info("[WeightManager] Restored container %s weight to %d (elevated for %.2fs)",
		state.ContainerID, state.CurrentWeight, elevatedDuration.Seconds())
}

func (wm *WeightManager) GetState(containerID string) *ContainerWeightState {
	wm.mu.Lock()
	defer wm.mu.Unlock()
	if state, exists := wm.states[containerID]; exists {
		copyState := *state
		return &copyState
	}
	return nil
}

func (wm *WeightManager) GetAllStates() []ContainerWeightState {
	wm.mu.Lock()
	defer wm.mu.Unlock()

	var states []ContainerWeightState
	for _, state := range wm.states {
		states = append(states, *state)
	}
	return states
}

func (wm *WeightManager) GetElevatedContainers() []ContainerWeightState {
	wm.mu.Lock()
	defer wm.mu.Unlock()

	var elevated []ContainerWeightState
	for _, state := range wm.states {
		if state.State == StateElevated {
			elevated = append(elevated, *state)
		}
	}
	return elevated
}

func (wm *WeightManager) GetSummary() string {
	wm.mu.Lock()
	defer wm.mu.Unlock()

	total := len(wm.states)
	elevated := 0
	waiting := 0

	for _, state := range wm.states {
		if state.State == StateElevated {
			elevated++
		}
		if state.IsWaiting {
			waiting++
		}
	}

	return fmt.Sprintf("Total: %d, Elevated: %d, Waiting: %d, StarvationTimeout: %v",
		total, elevated, waiting, wm.starvationTime)
}

func (wm *WeightManager) SetCooldownTime(d time.Duration) {
	wm.mu.Lock()
	defer wm.mu.Unlock()
	wm.cooldownTime = d
}

func (wm *WeightManager) SetMinBoostTime(d time.Duration) {
	wm.mu.Lock()
	defer wm.mu.Unlock()
	wm.minBoostTime = d
}

func (wm *WeightManager) ManualBoost(containerID string, duration time.Duration) error {
	wm.mu.Lock()
	defer wm.mu.Unlock()

	state, exists := wm.states[containerID]
	if !exists {
		return fmt.Errorf("container %s not registered", containerID)
	}

	if state.State != StateNormal {
		return fmt.Errorf("container %s already in state %v", containerID, state.State)
	}

	wm.elevateWeight(state)
	return nil
}

func (wm *WeightManager) ManualRestore(containerID string) error {
	wm.mu.Lock()
	defer wm.mu.Unlock()

	state, exists := wm.states[containerID]
	if !exists {
		return fmt.Errorf("container %s not registered", containerID)
	}

	if state.State != StateElevated {
		return fmt.Errorf("container %s not in elevated state", containerID)
	}

	wm.restoreWeight(state)
	return nil
}
