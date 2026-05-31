//go:build linux

package vgamepad

import (
	"encoding/binary"
	"fmt"
	"os"
	"syscall"
	"unsafe"
)

const (
	uinputPath = "/dev/uinput"
	uiDevCreate = 0x5501
	uiDevDestroy = 0x5502
	uiSetEvBit  = 0x40045501
	uiSetKeyBit = 0x40045502
	uiSetAbsBit = 0x40045503
)

const (
	evKey  = 0x01
	evAbs  = 0x03
	evSyn  = 0x00
	absX   = 0x00
	absY   = 0x01
	absZ   = 0x02
	absRZ  = 0x03
	absRx  = 0x04
	absRy  = 0x05
	synReport = 0
)

var btnMap = [17]uint16{
	0x130, 0x131, 0x133, 0x134,
	0x139, 0x132,
	0x136, 0x137, 0x138, 0x135,
	0x13a, 0x13b,
	0x220, 0x221, 0x222, 0x223,
	0x13c,
}

type uinputDev struct {
	file  *os.File
	index int
}

type uinputSetup struct {
	Name [80]byte
	ID   struct {
		Bustype uint16
		Vendor  uint16
		Product uint16
		Version uint16
	}
	FFEffectsMax uint32
	AbsMax       [64]int32
	AbsMin       [64]int32
	AbsFuzz      [64]int32
	AbsFlat      [64]int32
}

func newUinput(index int) (*uinputDev, error) {
	f, err := os.OpenFile(uinputPath, os.O_WRONLY|syscall.O_NONBLOCK, 0)
	if err != nil {
		return nil, fmt.Errorf("uinput: %w", err)
	}
	ioctl := func(req, arg uintptr) error {
		_, _, errno := syscall.Syscall(syscall.SYS_IOCTL, f.Fd(), req, arg)
		if errno != 0 {
			return errno
		}
		return nil
	}
	if err := ioctl(uiSetEvBit, evKey); err != nil {
		f.Close()
		return nil, err
	}
	if err := ioctl(uiSetEvBit, evAbs); err != nil {
		f.Close()
		return nil, err
	}
	for _, b := range btnMap {
		if err := ioctl(uiSetKeyBit, uintptr(b)); err != nil {
			f.Close()
			return nil, err
		}
	}
	abses := []uint{absX, absY, absZ, absRZ, absRx, absRy}
	for _, a := range abses {
		if err := ioctl(uiSetAbsBit, uintptr(a)); err != nil {
			f.Close()
			return nil, err
		}
	}
	setup := uinputSetup{}
	copy(setup.Name[:], fmt.Sprintf("CloudGamepad-%d", index))
	setup.ID.Bustype = 0x03 // BUS_USB
	setup.ID.Vendor = 0x1234
	setup.ID.Product = 0x5678
	setup.ID.Version = 1
	for i := range setup.AbsMax {
		setup.AbsMax[i] = 32767
		setup.AbsMin[i] = -32767
	}
	data := unsafe.Slice((*byte)(unsafe.Pointer(&setup)), unsafe.Sizeof(setup))
	if _, err := f.Write(data); err != nil {
		f.Close()
		return nil, err
	}
	if err := ioctl(uiDevCreate, 0); err != nil {
		f.Close()
		return nil, err
	}
	return &uinputDev{file: f, index: index}, nil
}

type inputEvent struct {
	Time  syscall.Timeval
	Type  uint16
	Code  uint16
	Value int32
}

func emit(f *os.File, typ, code uint16, value int32) error {
	ev := inputEvent{Type: typ, Code: code, Value: value}
	data := unsafe.Slice((*byte)(unsafe.Pointer(&ev)), unsafe.Sizeof(ev))
	_, err := f.Write(data)
	return err
}

func (u *uinputDev) Update(s State) error {
	for i, b := range btnMap {
		v := int32(0)
		if s.Buttons[i] {
			v = 1
		}
		if err := emit(u.file, evKey, b, v); err != nil {
			return err
		}
	}
	axisToCode := []struct {
		v float32
		c uint16
	}{
		{s.AxisLX, absX}, {s.AxisLY, absY},
		{s.TriggerL, absZ}, {s.TriggerR, absRZ},
		{s.AxisRX, absRx}, {s.AxisRY, absRy},
	}
	for _, a := range axisToCode {
		v := int32(clamp(a.v, -1, 1) * 32767)
		if a.c == absZ || a.c == absRZ {
			v = int32(clamp01(a.v) * 255)
		}
		if err := emit(u.file, evAbs, a.c, v); err != nil {
			return err
		}
	}
	return emit(u.file, evSyn, synReport, 0)
}

func (u *uinputDev) Close() error {
	ioctlClose(uintptr(u.file.Fd()), uiDevDestroy, 0)
	return u.file.Close()
}

func (u *uinputDev) Index() int { return u.index }

func clamp(v, lo, hi float32) float32 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}
func clamp01(v float32) float32 { return clamp(v, 0, 1) }

func _() {
	_ = binary.LittleEndian
}

func ioctlClose(fd, req, arg uintptr) {
	syscall.Syscall(syscall.SYS_IOCTL, fd, req, arg)
}
