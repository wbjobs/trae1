package resource

import (
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/lxc-migrate/lxc-migrate/internal/types"
)

func CheckTargetResources(targetHost string, requiredMem uint64, requiredDisk uint64, port int) (*types.ResourceInfo, error) {
	info, err := GetLocalResources()
	if err != nil {
		return nil, fmt.Errorf("get local resources: %w", err)
	}

	if info.AvailableMemory < requiredMem {
		return nil, fmt.Errorf("insufficient memory on target: available %d bytes, required %d bytes",
			info.AvailableMemory, requiredMem)
	}

	if info.FreeDisk < requiredDisk {
		return nil, fmt.Errorf("insufficient disk on target: available %d bytes, required %d bytes",
			info.FreeDisk, requiredDisk)
	}

	return info, nil
}

func GetLocalResources() (*types.ResourceInfo, error) {
	info := &types.ResourceInfo{}

	memTotal, memFree, memAvailable, err := readMemoryInfo()
	if err != nil {
		return nil, err
	}
	info.TotalMemory = memTotal
	info.FreeMemory = memFree
	info.AvailableMemory = memAvailable

	diskTotal, diskFree, err := readDiskInfo("/")
	if err != nil {
		return nil, err
	}
	info.TotalDisk = diskTotal
	info.FreeDisk = diskFree

	cpuNum, err := readCPUCount()
	if err != nil {
		return nil, err
	}
	info.CPUNum = cpuNum

	info.CPUFreePercent, _ = readCPUFreePercent()

	return info, nil
}

func readMemoryInfo() (total, free, available uint64, err error) {
	data, err := os.ReadFile("/proc/meminfo")
	if err != nil {
		return 0, 0, 0, fmt.Errorf("read /proc/meminfo: %w", err)
	}

	lines := strings.Split(string(data), "\n")
	for _, line := range lines {
		fields := strings.Fields(line)
		if len(fields) < 2 {
			continue
		}

		val, parseErr := strconv.ParseUint(fields[1], 10, 64)
		if parseErr != nil {
			continue
		}

		switch fields[0] {
		case "MemTotal:":
			total = val * 1024
		case "MemFree:":
			free = val * 1024
		case "MemAvailable:":
			available = val * 1024
		}
	}

	if available == 0 {
		available = free
	}

	return total, free, available, nil
}

type diskStats struct {
	total uint64
	free  uint64
}

func readCPUCount() (int, error) {
	data, err := os.ReadFile("/proc/cpuinfo")
	if err != nil {
		return 0, fmt.Errorf("read /proc/cpuinfo: %w", err)
	}

	count := 0
	for _, line := range strings.Split(string(data), "\n") {
		if strings.HasPrefix(line, "processor") {
			count++
		}
	}

	if count == 0 {
		return 0, fmt.Errorf("no CPU found in /proc/cpuinfo")
	}

	return count, nil
}

func readCPUFreePercent() (float64, error) {
	data, err := os.ReadFile("/proc/stat")
	if err != nil {
		return 0, fmt.Errorf("read /proc/stat: %w", err)
	}

	lines := strings.Split(string(data), "\n")
	for _, line := range lines {
		if strings.HasPrefix(line, "cpu ") {
			fields := strings.Fields(line)
			if len(fields) < 8 {
				continue
			}

			var total, idle uint64
			for i := 1; i < len(fields); i++ {
				val, _ := strconv.ParseUint(fields[i], 10, 64)
				total += val
				if i == 4 || i == 5 {
					idle += val
				}
			}

			if total == 0 {
				return 100, nil
			}
			return float64(idle) / float64(total) * 100, nil
		}
	}

	return 0, fmt.Errorf("could not read CPU info")
}

func GetContainerMemoryUsage(containerName string) (uint64, error) {
	cgroupPath := fmt.Sprintf("/sys/fs/cgroup/memory/lxc/%s/memory.usage_in_bytes", containerName)
	data, err := os.ReadFile(cgroupPath)
	if err != nil {
		altPath := fmt.Sprintf("/sys/fs/cgroup/memory/system.slice/lxc-%s.scope/memory.usage_in_bytes", containerName)
		data, err = os.ReadFile(altPath)
		if err != nil {
			return 0, fmt.Errorf("read container memory usage: %w", err)
		}
	}

	usage, err := strconv.ParseUint(strings.TrimSpace(string(data)), 10, 64)
	if err != nil {
		return 0, fmt.Errorf("parse memory usage: %w", err)
	}

	return usage, nil
}

func EstimateDiskUsage(containerName string) (uint64, error) {
	rootfsPath := fmt.Sprintf("/var/lib/lxc/%s/rootfs", containerName)
	return estimateDirSize(rootfsPath)
}

func estimateDirSize(path string) (uint64, error) {
	var size int64
	err := filepathWalk(path, func(_ string, info interface{}, err error) error {
		if err != nil {
			return err
		}
		if fi, ok := info.(os.FileInfo); ok && !fi.IsDir() {
			size += fi.Size()
		}
		return nil
	})
	return uint64(size), err
}

func filepathWalk(root string, fn func(string, interface{}, error) error) error {
	return walkDir(root, root, fn)
}

func walkDir(root, current string, fn func(string, interface{}, error) error) error {
	entries, err := os.ReadDir(current)
	if err != nil {
		return err
	}

	for _, entry := range entries {
		fullPath := current + "/" + entry.Name()
		info, err := entry.Info()
		if err != nil {
			if err := fn(fullPath, nil, err); err != nil {
				return err
			}
			continue
		}

		if err := fn(fullPath, info, nil); err != nil {
			return err
		}

		if info.IsDir() {
			if err := walkDir(root, fullPath, fn); err != nil {
				return err
			}
		}
	}

	return nil
}
