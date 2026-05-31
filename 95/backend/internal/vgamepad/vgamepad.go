package vgamepad

type Button int

const (
	BtnA Button = iota
	BtnB
	BtnX
	BtnY
	BtnStart
	BtnBack
	BtnLB
	BtnRB
	BtnLT
	BtnRT
	BtnLeftStick
	BtnRightStick
	BtnDpadUp
	BtnDpadDown
	BtnDpadLeft
	BtnDpadRight
	BtnGuide
)

type State struct {
	Buttons [17]bool
	AxisLX  float32
	AxisLY  float32
	AxisRX  float32
	AxisRY  float32
	TriggerL float32
	TriggerR float32
}

type Device interface {
	Update(s State) error
	Close() error
	Index() int
}

type Manager struct {
	devices []Device
	max     int
	factory func(int) (Device, error)
}

func NewManager(max int, factory func(int) (Device, error)) *Manager {
	return &Manager{
		devices: make([]Device, 0, max),
		max:     max,
		factory: factory,
	}
}

func (m *Manager) Acquire() (Device, error) {
	if len(m.devices) >= m.max {
		return nil, ErrTooMany
	}
	idx := len(m.devices)
	d, err := m.factory(idx)
	if err != nil {
		return nil, err
	}
	m.devices = append(m.devices, d)
	return d, nil
}

func (m *Manager) Release(d Device) {
	for i, x := range m.devices {
		if x == d {
			m.devices = append(m.devices[:i], m.devices[i+1:]...)
			_ = d.Close()
			return
		}
	}
}

func (m *Manager) Devices() []Device { return m.devices }
func (m *Manager) Max() int          { return m.max }
