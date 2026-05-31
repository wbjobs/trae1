//go:build windows

package vgamepad

import (
	"fmt"
	"sync"
	"syscall"
	"unsafe"
)

const (
	vigemBusClassGUID   = "{96E42B22-F5E9-42F8-B039-3C5879302681}"
	ioctlIndex          = 0x801
	fileDeviceUnknown   = 0x000000ff
	ioctlType           = fileDeviceUnknown
	methodBuffered      = 0
	anyAccess           = 0

	fileShareRead  = 1
	fileShareWrite = 2
	openExisting   = 3

	btnA       = 0x1000
	btnB       = 0x2000
	btnX       = 0x4000
	btnY       = 0x8000
	btnStart   = 0x0010
	btnBack    = 0x0020
	btnLB      = 0x0100
	btnRB      = 0x0200
	btnLT      = 0x0004
	btnRT      = 0x0008
	btnLeftStick  = 0x0040
	btnRightStick = 0x0080
	dpadUp     = 0x0001
	dpadDown   = 0x0002
	dpadLeft   = 0x0004
	dpadRight  = 0x0008
	btnGuide   = 0x0400
)

type vigemClient struct {
	mu     sync.Mutex
	handle syscall.Handle
	serial uint32
}

var (
	kernel32          = syscall.NewLazyDLL("kernel32.dll")
	setupAPI          = syscall.NewLazyDLL("setupapi.dll")
	procCreateFile    = kernel32.NewProc("CreateFileW")
	procDeviceIoCtrl  = kernel32.NewProc("DeviceIoControl")
	procCloseHandle   = kernel32.NewProc("CloseHandle")
	procGetClassDevs  = setupAPI.NewProc("SetupDiGetClassDevsW")
	procEnumDevInfo   = setupAPI.NewProc("SetupDiEnumDeviceInfo")
	procGetDevRegProp = setupAPI.NewProc("SetupDiGetDeviceRegistryPropertyW")
	procDestroyDevInfo = setupAPI.NewProc("SetupDiDestroyDeviceInfoList")
)

var globalClient *vigemClient
var globalClientMu sync.Mutex

func getClient() (*vigemClient, error) {
	globalClientMu.Lock()
	defer globalClientMu.Unlock()
	if globalClient != nil {
		return globalClient, nil
	}
	path, err := findViGEmBus()
	if err != nil {
		return nil, err
	}
	h, err := openDevice(path)
	if err != nil {
		return nil, err
	}
	globalClient = &vigemClient{handle: h}
	return globalClient, nil
}

func openDevice(path string) (syscall.Handle, error) {
	pathp, err := syscall.UTF16PtrFromString(path)
	if err != nil {
		return syscall.InvalidHandle, err
	}
	r1, _, e1 := procCreateFile.Call(
		uintptr(unsafe.Pointer(pathp)),
		anyAccess,
		fileShareRead|fileShareWrite,
		0,
		openExisting,
		0,
		0,
	)
	if r1 == ^uintptr(0) {
		if e1 != 0 {
			return syscall.InvalidHandle, e1
		}
		return syscall.InvalidHandle, syscall.EINVAL
	}
	return syscall.Handle(r1), nil
}

func findViGEmBus() (string, error) {
	guidp, _ := syscall.UTF16PtrFromString(vigemBusClassGUID)
	var guid syscall.GUID
	if err := syscall.CLSIDFromString(guidp, &guid); err != nil {
		return "", fmt.Errorf("ViGEm: %w", err)
	}
	r1, _, _ := procGetClassDevs.Call(
		uintptr(unsafe.Pointer(&guid)),
		0, 0,
		0x02|0x10, // DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
	)
	if r1 == ^uintptr(0) {
		return "", fmt.Errorf("ViGEm: SetupDiGetClassDevs failed")
	}
	hDevInfo := r1
	defer procDestroyDevInfo.Call(hDevInfo)

	type devInfoData struct {
		Size    uint32
		GUID    syscall.GUID
		DevInst uint32
		Reserved uintptr
	}
	did := devInfoData{Size: uint32(unsafe.Sizeof(devInfoData{}))}
	for i := uint32(0); ; i++ {
		r1, _, _ := procEnumDevInfo.Call(hDevInfo, uintptr(i), uintptr(unsafe.Pointer(&did)))
		if r1 == 0 {
			break
		}
		var data [256]uint16
		var reqType uint32
		var size uint32
		r1, _, _ = procGetDevRegProp.Call(
			hDevInfo, uintptr(unsafe.Pointer(&did)),
			0x00000001, // SPDRP_HARDWAREID
			uintptr(unsafe.Pointer(&reqType)),
			uintptr(unsafe.Pointer(&data[0])),
			256*2, uintptr(unsafe.Pointer(&size)), 0,
		)
		if r1 != 0 {
			return `\\.\ViGEmBus`, nil
		}
	}
	return "", fmt.Errorf("ViGEm: driver not found, install ViGEmBus")
}

func ctlCode(deviceType, function, method, access uint32) uint32 {
	return (deviceType << 16) | (access << 14) | (function << 2) | method
}

func (c *vigemClient) ioctl(code uint32, in, out []byte) error {
	var inp unsafe.Pointer
	if len(in) > 0 {
		inp = unsafe.Pointer(&in[0])
	}
	var outp unsafe.Pointer
	if len(out) > 0 {
		outp = unsafe.Pointer(&out[0])
	}
	var bytesReturned uint32
	r1, _, e1 := procDeviceIoCtrl.Call(
		uintptr(c.handle),
		uintptr(code),
		uintptr(inp), uintptr(len(in)),
		uintptr(outp), uintptr(len(out)),
		uintptr(unsafe.Pointer(&bytesReturned)), 0,
	)
	if r1 == 0 {
		if e1 != 0 {
			return e1
		}
		return syscall.EINVAL
	}
	return nil
}

type vigemPad struct {
	client *vigemClient
	serial uint32
	index  int
}

func (c *vigemClient) plugin(index int) (*vigemPad, error) {
	c.mu.Lock()
	c.serial++
	serial := c.serial
	c.mu.Unlock()
	plugIOCTL := ctlCode(ioctlType, ioctlIndex+1, methodBuffered, 0)
	in := make([]byte, 12)
	nativeEndian.PutUint32(in[0:], serial)
	nativeEndian.PutUint32(in[4:], 1) // X360
	nativeEndian.PutUint32(in[8:], 0)
	if err := c.ioctl(plugIOCTL, in, nil); err != nil {
		return nil, err
	}
	return &vigemPad{client: c, serial: serial, index: index}, nil
}

func (p *vigemPad) Update(s State) error {
	var report [24]byte
	nativeEndian.PutUint32(report[0:], p.serial)
	var buttons uint16
	if s.Buttons[BtnA] {
		buttons |= btnA
	}
	if s.Buttons[BtnB] {
		buttons |= btnB
	}
	if s.Buttons[BtnX] {
		buttons |= btnX
	}
	if s.Buttons[BtnY] {
		buttons |= btnY
	}
	if s.Buttons[BtnStart] {
		buttons |= btnStart
	}
	if s.Buttons[BtnBack] {
		buttons |= btnBack
	}
	if s.Buttons[BtnLB] {
		buttons |= btnLB
	}
	if s.Buttons[BtnRB] {
		buttons |= btnRB
	}
	if s.Buttons[BtnLeftStick] {
		buttons |= btnLeftStick
	}
	if s.Buttons[BtnRightStick] {
		buttons |= btnRightStick
	}
	if s.Buttons[BtnGuide] {
		buttons |= btnGuide
	}
	var dpad uint16
	if s.Buttons[BtnDpadUp] {
		dpad |= dpadUp
	}
	if s.Buttons[BtnDpadDown] {
		dpad |= dpadDown
	}
	if s.Buttons[BtnDpadLeft] {
		dpad |= dpadLeft
	}
	if s.Buttons[BtnDpadRight] {
		dpad |= dpadRight
	}
	lt := uint8(clamp01(s.TriggerL) * 255)
	rt := uint8(clamp01(s.TriggerR) * 255)
	if s.Buttons[BtnLT] && lt == 0 {
		lt = 255
	}
	if s.Buttons[BtnRT] && rt == 0 {
		rt = 255
	}
	lx := int16(clamp(s.AxisLX, -1, 1) * 32767)
	ly := int16(clamp(s.AxisLY, -1, 1) * 32767)
	rx := int16(clamp(s.AxisRX, -1, 1) * 32767)
	ry := int16(clamp(s.AxisRY, -1, 1) * 32767)
	nativeEndian.PutUint16(report[4:], buttons)
	nativeEndian.PutUint16(report[6:], dpad)
	report[8] = lt
	report[9] = rt
	nativeEndian.PutUint16(report[10:], uint16(lx))
	nativeEndian.PutUint16(report[12:], uint16(ly))
	nativeEndian.PutUint16(report[14:], uint16(rx))
	nativeEndian.PutUint16(report[16:], uint16(ry))
	ioctl := ctlCode(ioctlType, ioctlIndex+3, methodBuffered, 0)
	return p.client.ioctl(ioctl, report[:], nil)
}

func (p *vigemPad) Close() error {
	unplug := ctlCode(ioctlType, ioctlIndex+2, methodBuffered, 0)
	in := make([]byte, 4)
	nativeEndian.PutUint32(in, p.serial)
	return p.client.ioctl(unplug, in, nil)
}

func (p *vigemPad) Index() int { return p.index }

type nativeEndianT struct{}

var nativeEndian = nativeEndianT{}

func (nativeEndianT) PutUint16(b []byte, v uint16) {
	if isLittle() {
		b[0] = byte(v)
		b[1] = byte(v >> 8)
	} else {
		b[0] = byte(v >> 8)
		b[1] = byte(v)
	}
}
func (nativeEndianT) PutUint32(b []byte, v uint32) {
	if isLittle() {
		b[0] = byte(v)
		b[1] = byte(v >> 8)
		b[2] = byte(v >> 16)
		b[3] = byte(v >> 24)
	} else {
		b[0] = byte(v >> 24)
		b[1] = byte(v >> 16)
		b[2] = byte(v >> 8)
		b[3] = byte(v)
	}
}
func (nativeEndianT) Uint16(b []byte) uint16 {
	if isLittle() {
		return uint16(b[0]) | uint16(b[1])<<8
	}
	return uint16(b[1]) | uint16(b[0])<<8
}

func isLittle() bool {
	var i uint16 = 1
	return (*[2]byte)(unsafe.Pointer(&i))[0] == 1
}

func NewFactory() func(int) (Device, error) {
	return func(index int) (Device, error) {
		c, err := getClient()
		if err != nil {
			return nil, err
		}
		return c.plugin(index)
	}
}
