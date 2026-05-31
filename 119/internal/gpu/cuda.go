//go:build linux && cgo

package gpu

/*
#cgo LDFLAGS: -L/usr/local/cuda/lib64 -lcudart
#include <cuda_runtime.h>
#include <stdlib.h>
*/
import "C"

import (
	"fmt"
	"sync"
	"unsafe"
)

type CUDAMemoryManager struct {
	deviceID  int
	mu        sync.Mutex
	allocated []unsafe.Pointer
	verbose   bool
}

type CUDAMemcpyKind int

const (
	CudaMemcpyHostToHost     CUDAMemcpyKind = 0
	CudaMemcpyHostToDevice   CUDAMemcpyKind = 1
	CudaMemcpyDeviceToHost   CUDAMemcpyKind = 2
	CudaMemcpyDeviceToDevice CUDAMemcpyKind = 3
	CudaMemcpyDefault        CUDAMemcpyKind = 4
)

type CUDAError int

func (e CUDAError) Error() string {
	return fmt.Sprintf("CUDA error: %d", e)
}

const CUDA_SUCCESS = 0

func NewCUDAMemoryManager(deviceID int, verbose bool) (*CUDAMemoryManager, error) {
	mgr := &CUDAMemoryManager{
		deviceID: deviceID,
		verbose:  verbose,
	}

	if err := mgr.cudaSetDevice(); err != nil {
		return nil, err
	}

	return mgr, nil
}

func (m *CUDAMemoryManager) cudaSetDevice() error {
	result := C.cudaSetDevice(C.int(m.deviceID))
	if result != CUDA_SUCCESS {
		return CUDAError(result)
	}
	return nil
}

func (m *CUDAMemoryManager) HostAlloc(size uint64) (unsafe.Pointer, error) {
	var ptr unsafe.Pointer
	result := C.cudaMallocHost(&ptr, C.size_t(size))
	if result != CUDA_SUCCESS {
		return nil, CUDAError(result)
	}

	m.mu.Lock()
	m.allocated = append(m.allocated, ptr)
	m.mu.Unlock()

	return ptr, nil
}

func (m *CUDAMemoryManager) DeviceAlloc(size uint64) (unsafe.Pointer, error) {
	var ptr unsafe.Pointer
	result := C.cudaMalloc(&ptr, C.size_t(size))
	if result != CUDA_SUCCESS {
		return nil, CUDAError(result)
	}

	m.mu.Lock()
	m.allocated = append(m.allocated, ptr)
	m.mu.Unlock()

	return ptr, nil
}

func (m *CUDAMemoryManager) Memcpy(dst unsafe.Pointer, src unsafe.Pointer, size uint64, kind CUDAMemcpyKind) error {
	result := C.cudaMemcpy(dst, src, C.size_t(size), C.cudaMemcpyKind(kind))
	if result != CUDA_SUCCESS {
		return CUDAError(result)
	}
	return nil
}

func (m *CUDAMemoryManager) Free(ptr unsafe.Pointer) error {
	result := C.cudaFreeHost(ptr)
	if result != CUDA_SUCCESS {
		result = C.cudaFree(ptr)
		if result != CUDA_SUCCESS {
			return CUDAError(result)
		}
	}

	m.mu.Lock()
	for i, p := range m.allocated {
		if p == ptr {
			m.allocated = append(m.allocated[:i], m.allocated[i+1:]...)
			break
		}
	}
	m.mu.Unlock()

	return nil
}

func (m *CUDAMemoryManager) Cleanup() {
	m.mu.Lock()
	defer m.mu.Unlock()

	for _, ptr := range m.allocated {
		C.cudaFreeHost(ptr)
		C.cudaFree(ptr)
	}
	m.allocated = nil
}

func (m *CUDAMemoryManager) DeviceSynchronize() error {
	result := C.cudaDeviceSynchronize()
	if result != CUDA_SUCCESS {
		return CUDAError(result)
	}
	return nil
}

func (m *CUDAMemoryManager) SaveGPUState(devicePtr uint64, sizeBytes uint64) (*GPUMemoryBuffer, error) {
	hostPtr, err := m.HostAlloc(sizeBytes)
	if err != nil {
		return nil, fmt.Errorf("host alloc: %w", err)
	}

	if m.verbose {
		fmt.Printf("[gpu] saving %d bytes from GPU 0x%x to host\n", sizeBytes, devicePtr)
	}

	if err := m.Memcpy(hostPtr, unsafe.Pointer(devicePtr), sizeBytes, CudaMemcpyDeviceToHost); err != nil {
		m.Free(hostPtr)
		return nil, fmt.Errorf("cudaMemcpy D2H: %w", err)
	}

	if err := m.DeviceSynchronize(); err != nil {
		m.Free(hostPtr)
		return nil, fmt.Errorf("cudaDeviceSynchronize: %w", err)
	}

	hostBuffer := make([]byte, sizeBytes)
	C.memcpy(unsafe.Pointer(&hostBuffer[0]), hostPtr, C.size_t(sizeBytes))

	m.Free(hostPtr)

	return &GPUMemoryBuffer{
		SourceAddr: devicePtr,
		SizeBytes:  sizeBytes,
		HostBuffer: hostBuffer,
	}, nil
}

func (m *CUDAMemoryManager) RestoreGPUState(buffer *GPUMemoryBuffer, newDevicePtr uint64) error {
	if buffer == nil || len(buffer.HostBuffer) == 0 {
		return fmt.Errorf("empty buffer")
	}

	sizeBytes := uint64(len(buffer.HostBuffer))

	hostPtr, err := m.HostAlloc(sizeBytes)
	if err != nil {
		return fmt.Errorf("host alloc: %w", err)
	}
	defer m.Free(hostPtr)

	C.memcpy(hostPtr, unsafe.Pointer(&buffer.HostBuffer[0]), C.size_t(sizeBytes))

	if m.verbose {
		fmt.Printf("[gpu] restoring %d bytes from host to GPU 0x%x\n", sizeBytes, newDevicePtr)
	}

	if err := m.Memcpy(unsafe.Pointer(newDevicePtr), hostPtr, sizeBytes, CudaMemcpyHostToDevice); err != nil {
		return fmt.Errorf("cudaMemcpy H2D: %w", err)
	}

	if err := m.DeviceSynchronize(); err != nil {
		return fmt.Errorf("cudaDeviceSynchronize: %w", err)
	}

	buffer.TargetAddr = newDevicePtr
	return nil
}

func (m *CUDAMemoryManager) GetDeviceCount() (int, error) {
	var count C.int
	result := C.cudaGetDeviceCount(&count)
	if result != CUDA_SUCCESS {
		return 0, CUDAError(result)
	}
	return int(count), nil
}

func (m *CUDAMemoryManager) GetDeviceMemoryInfo() (free uint64, total uint64, err error) {
	var cFree, cTotal C.size_t
	result := C.cudaMemGetInfo(&cFree, &cTotal)
	if result != CUDA_SUCCESS {
		return 0, 0, CUDAError(result)
	}
	return uint64(cFree), uint64(cTotal), nil
}

func (m *CUDAMemoryManager) ResetDevice() error {
	result := C.cudaDeviceReset()
	if result != CUDA_SUCCESS {
		return CUDAError(result)
	}
	return nil
}
