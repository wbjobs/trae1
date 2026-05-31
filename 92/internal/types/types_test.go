package types

import "testing"

func TestGetPriorityWeight(t *testing.T) {
	tests := []struct {
		priority string
		expected int
	}{
		{PriorityHigh, 100},
		{PriorityMedium, 50},
		{PriorityLow, 10},
		{"unknown", 50},
		{"", 50},
	}

	for _, tt := range tests {
		t.Run(tt.priority, func(t *testing.T) {
			result := GetPriorityWeight(tt.priority)
			if result != tt.expected {
				t.Errorf("GetPriorityWeight(%s) = %d, want %d", tt.priority, result, tt.expected)
			}
		})
	}
}

func TestParseSize(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
		hasError bool
	}{
		{"", 0, false},
		{"1024", 1024, false},
		{"1KB", 1024, false},
		{"1MB", 1024 * 1024, false},
		{"1GB", 1024 * 1024 * 1024, false},
		{"10MB", 10 * 1024 * 1024, false},
		{"100MB", 100 * 1024 * 1024, false},
		{"1kb", 1024, false},
		{"1mb", 1024 * 1024, false},
		{"invalid", 0, true},
	}

	for _, tt := range tests {
		t.Run(tt.input, func(t *testing.T) {
			result, err := ParseSize(tt.input)
			if tt.hasError && err == nil {
				t.Errorf("ParseSize(%s) expected error, got nil", tt.input)
			}
			if !tt.hasError && err != nil {
				t.Errorf("ParseSize(%s) unexpected error: %v", tt.input, err)
			}
			if result != tt.expected {
				t.Errorf("ParseSize(%s) = %d, want %d", tt.input, result, tt.expected)
			}
		})
	}
}

func TestIOLimitStructure(t *testing.T) {
	limit := IOLimit{
		ReadBPS:   1024 * 1024,
		WriteBPS:  512 * 1024,
		ReadIOPS:  1000,
		WriteIOPS: 500,
		Priority:  PriorityHigh,
	}

	if limit.ReadBPS != 1048576 {
		t.Errorf("Expected ReadBPS = 1048576, got %d", limit.ReadBPS)
	}
	if limit.Priority != PriorityHigh {
		t.Errorf("Expected Priority = %s, got %s", PriorityHigh, limit.Priority)
	}
}
