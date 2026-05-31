package weight

import (
	"testing"
	"time"

	"github.com/io-qos/io-qos/internal/types"
)

func TestNewWeightManager(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	if wm == nil {
		t.Error("NewWeightManager returned nil")
	}
	if wm.starvationTime != 30*time.Second {
		t.Errorf("Expected starvationTime 30s, got %v", wm.starvationTime)
	}
	if wm.cgroupRoot != "/sys/fs/cgroup" {
		t.Errorf("Expected cgroupRoot /sys/fs/cgroup, got %s", wm.cgroupRoot)
	}
	if wm.cooldownTime != 5*time.Second {
		t.Errorf("Expected cooldownTime 5s, got %v", wm.cooldownTime)
	}
	if wm.minBoostTime != 10*time.Second {
		t.Errorf("Expected minBoostTime 10s, got %v", wm.minBoostTime)
	}
}

func TestRegisterContainer(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	wm.RegisterContainer("test-container", "test", "/docker/test", types.PriorityLow)

	state := wm.GetState("test-container")
	if state == nil {
		t.Fatal("Expected container state to be registered")
	}

	if state.ContainerID != "test-container" {
		t.Errorf("Expected ContainerID 'test-container', got '%s'", state.ContainerID)
	}
	if state.ContainerName != "test" {
		t.Errorf("Expected ContainerName 'test', got '%s'", state.ContainerName)
	}
	if state.CgroupPath != "/docker/test" {
		t.Errorf("Expected CgroupPath '/docker/test', got '%s'", state.CgroupPath)
	}
	if state.OriginalWeight != types.GetPriorityWeight(types.PriorityLow) {
		t.Errorf("Expected OriginalWeight %d, got %d",
			types.GetPriorityWeight(types.PriorityLow), state.OriginalWeight)
	}
	if state.CurrentWeight != state.OriginalWeight {
		t.Errorf("Expected CurrentWeight to equal OriginalWeight")
	}
	if state.State != StateNormal {
		t.Errorf("Expected StateNormal, got %v", state.State)
	}
	if state.BoostCount != 0 {
		t.Errorf("Expected BoostCount 0, got %d", state.BoostCount)
	}
}

func TestUnregisterContainer(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	wm.RegisterContainer("test-container", "test", "/docker/test", types.PriorityLow)

	if wm.GetState("test-container") == nil {
		t.Fatal("Expected container to be registered")
	}

	wm.UnregisterContainer("test-container")

	if wm.GetState("test-container") != nil {
		t.Error("Expected container to be unregistered")
	}
}

func TestGetStarvationTimeout(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	timeout := wm.GetStarvationTimeout()
	if timeout != 30*time.Second {
		t.Errorf("Expected timeout 30s, got %v", timeout)
	}

	wm.SetStarvationTimeout(60 * time.Second)
	timeout = wm.GetStarvationTimeout()
	if timeout != 60*time.Second {
		t.Errorf("Expected timeout 60s, got %v", timeout)
	}
}

func TestGetSummary(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	wm.RegisterContainer("container1", "c1", "/docker/c1", types.PriorityLow)
	wm.RegisterContainer("container2", "c2", "/docker/c2", types.PriorityMedium)

	summary := wm.GetSummary()

	if summary == "" {
		t.Error("Expected non-empty summary")
	}
}

func TestGetAllStates(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	wm.RegisterContainer("container1", "c1", "/docker/c1", types.PriorityLow)
	wm.RegisterContainer("container2", "c2", "/docker/c2", types.PriorityMedium)

	states := wm.GetAllStates()
	if len(states) != 2 {
		t.Errorf("Expected 2 states, got %d", len(states))
	}
}

func TestGetElevatedContainers(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	wm.RegisterContainer("container1", "c1", "/docker/c1", types.PriorityLow)
	wm.RegisterContainer("container2", "c2", "/docker/c2", types.PriorityMedium)

	elevated := wm.GetElevatedContainers()
	if len(elevated) != 0 {
		t.Errorf("Expected 0 elevated containers, got %d", len(elevated))
	}
}

func TestSetCooldownTime(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	wm.SetCooldownTime(10 * time.Second)
	if wm.cooldownTime != 10*time.Second {
		t.Errorf("Expected cooldownTime 10s, got %v", wm.cooldownTime)
	}
}

func TestSetMinBoostTime(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	wm.SetMinBoostTime(20 * time.Second)
	if wm.minBoostTime != 20*time.Second {
		t.Errorf("Expected minBoostTime 20s, got %v", wm.minBoostTime)
	}
}

func TestWeightStateString(t *testing.T) {
	tests := []struct {
		state    WeightState
		expected string
	}{
		{StateNormal, "normal"},
		{StateElevated, "ELEVATED"},
		{StateRecovering, "recovering"},
	}

	for _, tt := range tests {
		result := formatWeightState(tt.state)
		if result != tt.expected {
			t.Errorf("formatWeightState(%v) = %s, want %s", tt.state, result, tt.expected)
		}
	}
}

func TestMultipleContainers(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	containers := []struct {
		id       string
		name     string
		path     string
		priority string
	}{
		{"mysql", "mysql-prod", "/docker/mysql", types.PriorityHigh},
		{"redis", "redis-cache", "/docker/redis", types.PriorityMedium},
		{"logger", "log-collector", "/docker/logger", types.PriorityLow},
		{"nginx", "nginx-web", "/docker/nginx", types.PriorityLow},
	}

	for _, c := range containers {
		wm.RegisterContainer(c.id, c.name, c.path, c.priority)
	}

	states := wm.GetAllStates()
	if len(states) != 4 {
		t.Errorf("Expected 4 states, got %d", len(states))
	}

	summary := wm.GetSummary()
	if summary == "" {
		t.Error("Expected non-empty summary")
	}
}

func TestStateIsolation(t *testing.T) {
	wm := NewWeightManager(30*time.Second, "/sys/fs/cgroup")

	wm.RegisterContainer("container1", "c1", "/docker/c1", types.PriorityLow)

	state1 := wm.GetState("container1")
	state2 := wm.GetState("container1")

	if state1 == state2 {
		t.Error("GetState should return a copy, not the original")
	}

	state1.CurrentWeight = 999
	state2 = wm.GetState("container1")
	if state2.CurrentWeight == 999 {
		t.Error("Modifying returned state should not affect internal state")
	}
}
