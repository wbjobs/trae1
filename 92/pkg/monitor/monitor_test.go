package monitor

import (
	"testing"
	"time"
)

func TestFormatBytes(t *testing.T) {
	tests := []struct {
		input    float64
		expected string
	}{
		{0, "0.00 B"},
		{512, "512.00 B"},
		{1024, "1.00 KB"},
		{1024 * 1024, "1.00 MB"},
		{1024 * 1024 * 1024, "1.00 GB"},
		{10 * 1024 * 1024, "10.00 MB"},
		{100 * 1024 * 1024, "100.00 MB"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := FormatBytes(tt.input)
			if result != tt.expected {
				t.Errorf("FormatBytes(%f) = %s, want %s", tt.input, result, tt.expected)
			}
		})
	}
}

func TestFormatIOPS(t *testing.T) {
	tests := []struct {
		input    float64
		expected string
	}{
		{0, "0"},
		{500, "500"},
		{1000, "1.00 K"},
		{1500, "1.50 K"},
		{1000000, "1.00 M"},
		{1500000, "1.50 M"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := FormatIOPS(tt.input)
			if result != tt.expected {
				t.Errorf("FormatIOPS(%f) = %s, want %s", tt.input, result, tt.expected)
			}
		})
	}
}

func TestFormatBandwidth(t *testing.T) {
	tests := []struct {
		input    float64
		expected string
	}{
		{1024, "1.00 KB/s"},
		{1024 * 1024, "1.00 MB/s"},
	}

	for _, tt := range tests {
		t.Run(tt.expected, func(t *testing.T) {
			result := FormatBandwidth(tt.input)
			if result != tt.expected {
				t.Errorf("FormatBandwidth(%f) = %s, want %s", tt.input, result, tt.expected)
			}
		})
	}
}

func TestNewMonitor(t *testing.T) {
	m := NewMonitor(1 * time.Second)
	if m == nil {
		t.Error("NewMonitor returned nil")
	}
	if m.interval != 1*time.Second {
		t.Errorf("Expected interval 1s, got %v", m.interval)
	}
	if m.cgroupRoot != "/sys/fs/cgroup" {
		t.Errorf("Expected cgroupRoot /sys/fs/cgroup, got %s", m.cgroupRoot)
	}
}

func TestNewMonitorWithRoot(t *testing.T) {
	m := NewMonitorWithRoot(2*time.Second, "/custom/cgroup")
	if m == nil {
		t.Error("NewMonitorWithRoot returned nil")
	}
	if m.interval != 2*time.Second {
		t.Errorf("Expected interval 2s, got %v", m.interval)
	}
	if m.cgroupRoot != "/custom/cgroup" {
		t.Errorf("Expected cgroupRoot /custom/cgroup, got %s", m.cgroupRoot)
	}
}

func TestContainerTarget(t *testing.T) {
	target := ContainerTarget{
		ContainerID:   "test-container-id",
		ContainerName: "test-container",
		CgroupPath:    "/docker/abc123",
	}

	if target.ContainerID != "test-container-id" {
		t.Errorf("Unexpected ContainerID: %s", target.ContainerID)
	}
	if target.ContainerName != "test-container" {
		t.Errorf("Unexpected ContainerName: %s", target.ContainerName)
	}
	if target.CgroupPath != "/docker/abc123" {
		t.Errorf("Unexpected CgroupPath: %s", target.CgroupPath)
	}
}
