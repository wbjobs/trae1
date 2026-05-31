//go:build !windows

package vgamepad

func NewAutoFactory() func(int) (Device, error) {
	return NewMockFactory()
}
