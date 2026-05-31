package vgamepad

import (
	"fmt"
	"sync"
)

type mockPad struct {
	mu    sync.RWMutex
	idx   int
	state State
}

func (m *mockPad) Update(s State) error {
	m.mu.Lock()
	m.state = s
	m.mu.Unlock()
	return nil
}
func (m *mockPad) Close() error { return nil }
func (m *mockPad) Index() int  { return m.idx }

func (m *mockPad) Snapshot() State {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return m.state
}

func NewMockFactory() func(int) (Device, error) {
	return func(i int) (Device, error) {
		if i >= 4 {
			return nil, fmt.Errorf("mock: max 4")
		}
		return &mockPad{idx: i}, nil
	}
}
