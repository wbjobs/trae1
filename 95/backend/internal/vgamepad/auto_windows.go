//go:build windows

package vgamepad

func NewAutoFactory() func(int) (Device, error) {
	return func(i int) (Device, error) {
		f := NewFactory()
		d, err := f(i)
		if err != nil {
			return NewMockFactory()(i)
		}
		return d, nil
	}
}
