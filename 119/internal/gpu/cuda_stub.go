//go:build !linux || !cgo

package gpu

import (
	"fmt"
	"unsafe"
)

type CUDAMemoryManager struct {
	deviceID int
	verbose  bool
}

type CUDAMemcpyKind int

const (
	CudaMemcpyHostToHost     CUDAMemcpyKind = 0
	CudaMemcpyHostToDevice   CUDAMemcpyKind = 1
	CudaMemcpyDeviceToHost   CUDAMemcpyKind = 2
	CudaMemcpyDeviceToDevice CUDAMemcpyKind = 3
	CudaMemcpyDefault        CUDAMemcpyKind = 4
)

func NewCUDAMemoryManager(deviceID int, verbose bool) (*CUDAMemoryManager, error) {
	return nil, fmt.Errorf("CUDA support requires Linux with CGO enabled")
}

func (m *CUDAMemoryManager) HostAlloc(size uint64) (unsafe.Pointer, error) {
	return nil, fmt.Errorf("CUDA not available")
}

func (m *CUDAMemoryManager) DeviceAlloc(size uint64) (unsafe.Pointer, error) {
	return nil, fmt.Errorf("CUDA not available")
}

func (m *CUDAMemoryManager) Memcpy(dst unsafe.Pointer, src unsafe.Pointer, size uint64, kind CUDAMemcpyKind) error {
	return fmt.Errorf("CUDA not available")
}

func (m *CUDAMemoryManager) Free(ptr unsafe.Pointer) error {
	return fmt.Errorf("CUDA not available")
}

func (m *CUDAMemoryManager) Cleanup() {}

func (m *CUDAMemoryManager) DeviceSynchronize() error {
	return fmt.Errorf("CUDA not available")
}

func (m *CUDAMemoryManager) SaveGPUState(devicePtr uint64, sizeBytes uint64) (*GPUMemoryBuffer, error) {
	return nil, fmt.Errorf("CUDA not available")
}

func (m *CUDAMemoryManager) RestoreGPUState(buffer *GPUMemoryBuffer, newDevicePtr uint64) error {
	return fmt.Errorf("CUDA not available")
}

func (m *CUDAMemoryManager) GetDeviceCount() (int, error) {
	return 0, fmt.Errorf("CUDA not available")
}

func (m *CUDAMemoryManager) GetDeviceMemoryInfo() (free uint64, total uint64, err error) {
	return 0, 0, fmt.Errorf("CUDA not available")
}

func (m *CUDAMemoryManager) ResetDevice() error {
	return fmt.Errorf("CUDA not available")
}
