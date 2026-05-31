package cgroup

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/io-qos/io-qos/internal/types"
)

const (
	DefaultCgroupRoot = "/sys/fs/cgroup"
	IOMaxFile         = "io.max"
	IOWeightFile      = "io.weight"
	IOStatFile        = "io.stat"
	IOPressureFile    = "io.pressure"
)

type IOController struct {
	cgroupRoot string
}

func NewIOController() *IOController {
	return &IOController{
		cgroupRoot: DefaultCgroupRoot,
	}
}

func NewIOControllerWithRoot(root string) *IOController {
	return &IOController{
		cgroupRoot: root,
	}
}

func (c *IOController) SetIOLimit(cgroupPath string, device types.DeviceInfo, limit types.IOLimit) error {
	fullPath := filepath.Join(c.cgroupRoot, cgroupPath, IOMaxFile)

	readBPSVal := "max"
	if limit.ReadBPS > 0 {
		readBPSVal = fmt.Sprintf("%d", limit.ReadBPS)
	}

	writeBPSVal := "max"
	if limit.WriteBPS > 0 {
		writeBPSVal = fmt.Sprintf("%d", limit.WriteBPS)
	}

	readIOPSVal := "max"
	if limit.ReadIOPS > 0 {
		readIOPSVal = fmt.Sprintf("%d", limit.ReadIOPS)
	}

	writeIOPSVal := "max"
	if limit.WriteIOPS > 0 {
		writeIOPSVal = fmt.Sprintf("%d", limit.WriteIOPS)
	}

	content := fmt.Sprintf("%d:%d rbps=%s wbps=%s riops=%s wiops=%s\n",
		device.Major, device.Minor,
		readBPSVal, writeBPSVal,
		readIOPSVal, writeIOPSVal)

	return os.WriteFile(fullPath, []byte(content), 0644)
}

func (c *IOController) SetIOPriority(cgroupPath string, device types.DeviceInfo, priority string) error {
	fullPath := filepath.Join(c.cgroupRoot, cgroupPath, IOWeightFile)
	weight := types.GetPriorityWeight(priority)
	content := fmt.Sprintf("%d:%d %d\n", device.Major, device.Minor, weight)
	return os.WriteFile(fullPath, []byte(content), 0644)
}

func (c *IOController) GetIOLimit(cgroupPath string, device types.DeviceInfo) (types.IOLimit, error) {
	fullPath := filepath.Join(c.cgroupRoot, cgroupPath, IOMaxFile)
	content, err := os.ReadFile(fullPath)
	if err != nil {
		return types.IOLimit{}, err
	}

	var limit types.IOLimit
	scanner := bufio.NewScanner(strings.NewReader(string(content)))
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.Fields(line)
		if len(parts) < 2 {
			continue
		}

		devParts := strings.Split(parts[0], ":")
		if len(devParts) != 2 {
			continue
		}
		major, _ := strconv.Atoi(devParts[0])
		minor, _ := strconv.Atoi(devParts[1])
		if major != device.Major || minor != device.Minor {
			continue
		}

		for _, part := range parts[1:] {
			kv := strings.Split(part, "=")
			if len(kv) != 2 {
				continue
			}
			key := kv[0]
			val, err := strconv.ParseInt(kv[1], 10, 64)
			if err != nil && kv[1] != "max" {
				continue
			}

			switch key {
			case "rbps":
				limit.ReadBPS = val
			case "wbps":
				limit.WriteBPS = val
			case "riops":
				limit.ReadIOPS = val
			case "wiops":
				limit.WriteIOPS = val
			}
		}
	}

	return limit, nil
}

func (c *IOController) GetIOPriority(cgroupPath string, device types.DeviceInfo) (int, error) {
	fullPath := filepath.Join(c.cgroupRoot, cgroupPath, IOWeightFile)
	content, err := os.ReadFile(fullPath)
	if err != nil {
		return 0, err
	}

	scanner := bufio.NewScanner(strings.NewReader(string(content)))
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.Fields(line)
		if len(parts) < 2 {
			continue
		}

		devParts := strings.Split(parts[0], ":")
		if len(devParts) != 2 {
			continue
		}
		major, _ := strconv.Atoi(devParts[0])
		minor, _ := strconv.Atoi(devParts[1])
		if major != device.Major || minor != device.Minor {
			continue
		}

		weight, err := strconv.Atoi(parts[1])
		if err != nil {
			continue
		}
		return weight, nil
	}

	return 50, nil
}

func (c *IOController) GetIOStats(cgroupPath string) (map[types.DeviceInfo]types.IOStats, error) {
	fullPath := filepath.Join(c.cgroupRoot, cgroupPath, IOStatFile)
	content, err := os.ReadFile(fullPath)
	if err != nil {
		return nil, err
	}

	stats := make(map[types.DeviceInfo]types.IOStats)
	scanner := bufio.NewScanner(strings.NewReader(string(content)))
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.Fields(line)
		if len(parts) < 2 {
			continue
		}

		devParts := strings.Split(parts[0], ":")
		if len(devParts) != 2 {
			continue
		}
		major, _ := strconv.Atoi(devParts[0])
		minor, _ := strconv.Atoi(devParts[1])
		device := types.DeviceInfo{Major: major, Minor: minor}

		var stat types.IOStats
		stat.Timestamp = time.Now()

		for _, part := range parts[1:] {
			kv := strings.Split(part, "=")
			if len(kv) != 2 {
				continue
			}
			key := kv[0]
			val, err := strconv.ParseUint(kv[1], 10, 64)
			if err != nil {
				continue
			}

			switch key {
			case "rbytes":
				stat.ReadBytes = val
			case "wbytes":
				stat.WriteBytes = val
			case "rios":
				stat.ReadIOPS = float64(val)
			case "wios":
				stat.WriteIOPS = float64(val)
			}
		}

		stats[device] = stat
	}

	return stats, nil
}

func (c *IOController) GetIOPressure(cgroupPath string) (uint64, uint64, error) {
	fullPath := filepath.Join(c.cgroupRoot, cgroupPath, IOPressureFile)
	content, err := os.ReadFile(fullPath)
	if err != nil {
		return 0, 0, err
	}

	var queueLength, waitTime uint64
	scanner := bufio.NewScanner(strings.NewReader(string(content)))
	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, "some avg10=") {
			continue
		}
		if strings.HasPrefix(line, "full avg10=") {
			continue
		}
		if strings.Contains(line, "avg60") {
			continue
		}
	}

	return queueLength, waitTime, nil
}

func (c *IOController) GetBlockDevices() ([]types.DeviceInfo, error) {
	var devices []types.DeviceInfo

	entries, err := os.ReadDir("/sys/block")
	if err != nil {
		return nil, err
	}

	for _, entry := range entries {
		devPath := filepath.Join("/sys/block", entry.Name(), "dev")
		content, err := os.ReadFile(devPath)
		if err != nil {
			continue
		}

		devStr := strings.TrimSpace(string(content))
		parts := strings.Split(devStr, ":")
		if len(parts) != 2 {
			continue
		}

		major, err := strconv.Atoi(parts[0])
		if err != nil {
			continue
		}
		minor, err := strconv.Atoi(parts[1])
		if err != nil {
			continue
		}

		devices = append(devices, types.DeviceInfo{
			Major: major,
			Minor: minor,
			Name:  entry.Name(),
		})
	}

	return devices, nil
}

func (c *IOController) CgroupExists(cgroupPath string) bool {
	fullPath := filepath.Join(c.cgroupRoot, cgroupPath)
	_, err := os.Stat(fullPath)
	return err == nil
}

func (c *IOController) ApplyLimits(cgroupPath string, limits types.IOLimit) error {
	devices, err := c.GetBlockDevices()
	if err != nil {
		return fmt.Errorf("failed to get block devices: %w", err)
	}

	for _, dev := range devices {
		if err := c.SetIOLimit(cgroupPath, dev, limits); err != nil {
			return fmt.Errorf("failed to set io limit for %s: %w", dev.Name, err)
		}

		if limits.Priority != "" {
			if err := c.SetIOPriority(cgroupPath, dev, limits.Priority); err != nil {
				return fmt.Errorf("failed to set io priority for %s: %w", dev.Name, err)
			}
		}
	}

	return nil
}

func (c *IOController) GetCurrentLimits(cgroupPath string) (types.IOLimit, error) {
	devices, err := c.GetBlockDevices()
	if err != nil {
		return types.IOLimit{}, err
	}

	if len(devices) == 0 {
		return types.IOLimit{}, fmt.Errorf("no block devices found")
	}

	return c.GetIOLimit(cgroupPath, devices[0])
}
