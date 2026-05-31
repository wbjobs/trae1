package gpu

import (
	"encoding/json"
	"fmt"
	"net"
	"os"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
	"time"
)

type GPUInfo struct {
	Index       int         `json:"index"`
	Name        string      `json:"name"`
	UUID        string      `json:"uuid"`
	TotalMemory int64       `json:"total_memory_mb"`
	FreeMemory  int64       `json:"free_memory_mb"`
	UsedMemory  int64       `json:"used_memory_mb"`
	Temperature int         `json:"temperature"`
	PowerUsage  int         `json:"power_usage_mw"`
	MIGEnabled  bool        `json:"mig_enabled"`
	MIGDevices  []MIGDevice `json:"mig_devices,omitempty"`
}

type MIGDevice struct {
	Index    int    `json:"index"`
	GIID     int    `json:"gi_id"`
	CIID     int    `json:"ci_id"`
	Name     string `json:"name"`
	UUID     string `json:"uuid"`
	MemoryMB int64  `json:"memory_mb"`
}

type GPUDeviceMapping struct {
	SourceGPUIndex int    `json:"source_gpu_index"`
	TargetGPUIndex int    `json:"target_gpu_index"`
	SourceMIGUUID  string `json:"source_mig_uuid,omitempty"`
	TargetMIGUUID  string `json:"target_mig_uuid,omitempty"`
}

type GPUState struct {
	HasGPU         bool               `json:"has_gpu"`
	GPUs           []GPUInfo          `json:"gpus"`
	DeviceMappings []GPUDeviceMapping `json:"device_mappings"`
	StateSaved     bool               `json:"state_saved"`
	StateFile      string             `json:"state_file"`
}

type GPUSaveResult struct {
	Success     bool             `json:"success"`
	Processes   []GPUProcessInfo `json:"processes"`
	TotalMemory int64            `json:"total_memory_mb"`
	StateSize   int64            `json:"state_size_bytes"`
	Error       string           `json:"error,omitempty"`
}

type GPUProcessInfo struct {
	PID         int    `json:"pid"`
	ProcessName string `json:"process_name"`
	UsedMemory  int64  `json:"used_memory_mb"`
	GPUIndex    int    `json:"gpu_index"`
}

type GPURestoreResult struct {
	Success bool   `json:"success"`
	Error   string `json:"error,omitempty"`
}

const nvidiaSMIPath = "nvidia-smi"

func DetectGPU() (*GPUState, error) {
	if _, err := exec.LookPath(nvidiaSMIPath); err != nil {
		return &GPUState{HasGPU: false}, nil
	}

	cmd := exec.Command(nvidiaSMIPath, "-L")
	output, err := cmd.Output()
	if err != nil {
		return &GPUState{HasGPU: false}, nil
	}

	gpuList := strings.TrimSpace(string(output))
	if gpuList == "" {
		return &GPUState{HasGPU: false}, nil
	}

	state := &GPUState{HasGPU: true}

	gpus, err := queryGPUInfo()
	if err != nil {
		return state, err
	}
	state.GPUs = gpus

	for i := range state.GPUs {
		migDevices, err := queryMIGDevices(i)
		if err == nil && len(migDevices) > 0 {
			state.GPUs[i].MIGEnabled = true
			state.GPUs[i].MIGDevices = migDevices
		}
	}

	return state, nil
}

func queryGPUInfo() ([]GPUInfo, error) {
	queryFields := "index,name,uuid,memory.total,memory.free,memory.used,temperature.gpu,power.draw"
	cmd := exec.Command(nvidiaSMIPath,
		"--query-gpu="+queryFields,
		"--format=csv,noheader,nounits")

	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("查询GPU信息失败: %w", err)
	}

	var gpus []GPUInfo
	lines := strings.Split(strings.TrimSpace(string(output)), "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		fields := strings.Split(line, ",")
		if len(fields) < 8 {
			continue
		}

		gpu := GPUInfo{}
		gpu.Index, _ = strconv.Atoi(strings.TrimSpace(fields[0]))
		gpu.Name = strings.TrimSpace(fields[1])
		gpu.UUID = strings.TrimSpace(fields[2])
		gpu.TotalMemory, _ = strconv.ParseInt(strings.TrimSpace(fields[3]), 10, 64)
		gpu.FreeMemory, _ = strconv.ParseInt(strings.TrimSpace(fields[4]), 10, 64)
		gpu.UsedMemory, _ = strconv.ParseInt(strings.TrimSpace(fields[5]), 10, 64)
		gpu.Temperature, _ = strconv.Atoi(strings.TrimSpace(fields[6]))
		gpu.PowerUsage, _ = strconv.Atoi(strings.TrimSpace(fields[7]))

		gpus = append(gpus, gpu)
	}

	return gpus, nil
}

func queryMIGDevices(gpuIndex int) ([]MIGDevice, error) {
	cmd := exec.Command(nvidiaSMIPath,
		"-i", strconv.Itoa(gpuIndex),
		"--query-mig-mode")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return nil, err
	}

	if !strings.Contains(string(output), "Enabled") {
		return nil, nil
	}

	cmd = exec.Command(nvidiaSMIPath,
		"--query-compute-apps=gpu_uuid,pid,process_name,used_gpu_memory",
		"--format=csv,noheader,nounits")
	output, err = cmd.Output()
	if err != nil {
		return nil, err
	}

	var devices []MIGDevice
	lines := strings.Split(strings.TrimSpace(string(output)), "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "No ") {
			continue
		}
		fields := strings.Split(line, ",")
		if len(fields) >= 4 {
			mem, _ := strconv.ParseInt(strings.TrimSpace(fields[3]), 10, 64)
			devices = append(devices, MIGDevice{
				Name:     strings.TrimSpace(fields[2]),
				UUID:     strings.TrimSpace(fields[0]),
				MemoryMB: mem,
			})
		}
	}

	return devices, nil
}

func QueryGPUProcesses() ([]GPUProcessInfo, error) {
	cmd := exec.Command(nvidiaSMIPath,
		"--query-compute-apps=gpu_uuid,pid,process_name,used_gpu_memory",
		"--format=csv,noheader,nounits")

	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("查询GPU进程失败: %w", err)
	}

	var processes []GPUProcessInfo
	lines := strings.Split(strings.TrimSpace(string(output)), "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "No ") {
			continue
		}

		fields := strings.Split(line, ",")
		if len(fields) < 4 {
			continue
		}

		pid, _ := strconv.Atoi(strings.TrimSpace(fields[1]))
		mem, _ := strconv.ParseInt(strings.TrimSpace(fields[3]), 10, 64)

		processes = append(processes, GPUProcessInfo{
			PID:         pid,
			ProcessName: strings.TrimSpace(fields[2]),
			UsedMemory:  mem,
			GPUIndex:    0,
		})
	}

	return processes, nil
}

func NotifyGPUAppsRelease(containerName string, processes []GPUProcessInfo) error {
	fmt.Printf("  通知容器内GPU应用释放GPU资源 (SIGUSR1)...\n")

	for _, proc := range processes {
		cmd := exec.Command("lxc-attach", "-n", containerName, "--",
			"kill", "-USR1", strconv.Itoa(proc.PID))

		output, err := cmd.CombinedOutput()
		if err != nil {
			fmt.Printf("    警告: 向PID %d (%s) 发送SIGUSR1失败: %v\n",
				proc.PID, proc.ProcessName, err)
			continue
		}
		fmt.Printf("    已通知进程 %d (%s) 释放GPU\n", proc.PID, proc.ProcessName)
		_ = output
	}

	time.Sleep(2 * time.Second)
	return nil
}

func NotifyGPUAppsRestore(containerName string, processes []GPUProcessInfo) error {
	fmt.Printf("  通知容器内GPU应用恢复GPU状态 (SIGUSR2)...\n")

	for _, proc := range processes {
		cmd := exec.Command("lxc-attach", "-n", containerName, "--",
			"kill", "-USR2", strconv.Itoa(proc.PID))

		output, err := cmd.CombinedOutput()
		if err != nil {
			fmt.Printf("    警告: 向PID %d (%s) 发送SIGUSR2失败: %v\n",
				proc.PID, proc.ProcessName, err)
			continue
		}
		fmt.Printf("    已通知进程 %d (%s) 恢复GPU\n", proc.PID, proc.ProcessName)
		_ = output
	}

	return nil
}

func SaveGPUState(containerName string, checkpointDir string, processes []GPUProcessInfo) (*GPUSaveResult, error) {
	result := &GPUSaveResult{
		Success:   false,
		Processes: processes,
	}

	var totalMem int64
	for _, proc := range processes {
		totalMem += proc.UsedMemory
	}
	result.TotalMemory = totalMem

	stateFile := fmt.Sprintf("%s/gpu_state.json", checkpointDir)

	saveScript := fmt.Sprintf(`
cat > /tmp/gpu_save_state.sh << 'SCRIPT_EOF'
#!/bin/bash
CONTAINER_NAME="%s"
STATE_DIR="%s"

echo "开始保存GPU状态..."

lxc-attach -n "$CONTAINER_NAME" -- bash -c '
    for pid_path in /proc/[0-9]*; do
        pid=$(basename "$pid_path")
        if [ -f "$pid_path/environ" ]; then
            environ=$(tr "\0" "\n" < "$pid_path/environ" 2>/dev/null | grep -c "CUDA")
            if [ "$environ" -gt 0 ]; then
                echo "发现CUDA进程: PID=$pid"
                cat "$pid_path/status" 2>/dev/null | grep -E "Name|VmRSS"
            fi
        fi
    done
' > "$STATE_DIR/gpu_processes_info.txt" 2>&1

echo "GPU进程信息已保存"
SCRIPT_EOF
chmod +x /tmp/gpu_save_state.sh
bash /tmp/gpu_save_state.sh
`, containerName, checkpointDir)

	cmd := exec.Command("bash", "-c", saveScript)
	output, err := cmd.CombinedOutput()
	if err != nil {
		result.Error = fmt.Sprintf("保存GPU状态脚本执行失败: %v", err)
		return result, err
	}
	_ = output

	stateData := map[string]interface{}{
		"container":    containerName,
		"timestamp":    time.Now().Unix(),
		"processes":    processes,
		"total_memory": totalMem,
	}

	data, err := json.MarshalIndent(stateData, "", "  ")
	if err != nil {
		result.Error = fmt.Sprintf("序列化GPU状态失败: %v", err)
		return result, err
	}

	if err := os.WriteFile(stateFile, data, 0644); err != nil {
		result.Error = fmt.Sprintf("写入GPU状态文件失败: %v", err)
		return result, err
	}

	result.StateSize = int64(len(data))
	result.StateFile = stateFile
	result.Success = true

	fmt.Printf("    GPU状态已保存: %d 个进程, %d MB GPU显存\n",
		len(processes), totalMem)

	return result, nil
}

func RestoreGPUState(containerName string, checkpointDir string, mappings []GPUDeviceMapping) (*GPURestoreResult, error) {
	result := &GPURestoreResult{
		Success: false,
	}

	stateFile := fmt.Sprintf("%s/gpu_state.json", checkpointDir)

	data, err := os.ReadFile(stateFile)
	if err != nil {
		if os.IsNotExist(err) {
			result.Success = true
			return result, nil
		}
		result.Error = fmt.Sprintf("读取GPU状态文件失败: %v", err)
		return result, err
	}

	var stateData map[string]interface{}
	if err := json.Unmarshal(data, &stateData); err != nil {
		result.Error = fmt.Sprintf("解析GPU状态失败: %v", err)
		return result, err
	}

	processesRaw, _ := stateData["processes"].([]interface{})
	var processes []GPUProcessInfo
	for _, p := range processesRaw {
		if pd, ok := p.(map[string]interface{}); ok {
			proc := GPUProcessInfo{}
			if pid, ok := pd["pid"].(float64); ok {
				proc.PID = int(pid)
			}
			if name, ok := pd["process_name"].(string); ok {
				proc.ProcessName = name
			}
			if mem, ok := pd["used_memory_mb"].(float64); ok {
				proc.UsedMemory = int64(mem)
			}
			processes = append(processes, proc)
		}
	}

	if len(mappings) > 0 {
		if err := applyGPUDeviceMapping(containerName, mappings); err != nil {
			result.Error = fmt.Sprintf("应用GPU设备映射失败: %v", err)
			return result, err
		}
	}

	restoreScript := fmt.Sprintf(`
cat > /tmp/gpu_restore_state.sh << 'SCRIPT_EOF'
#!/bin/bash
CONTAINER_NAME="%s"
STATE_DIR="%s"

echo "开始恢复GPU状态..."

lxc-attach -n "$CONTAINER_NAME" -- bash -c '
    echo "GPU环境变量设置:"
    env | grep -i cuda || true
    env | grep -i nvidia || true
    
    ldconfig -p | grep -i cuda || true
    
    if command -v nvidia-smi &> /dev/null; then
        nvidia-smi || true
    fi
    
    echo "GPU环境检查完成"
' >> "$STATE_DIR/gpu_restore.log" 2>&1

echo "GPU状态恢复完成"
SCRIPT_EOF
chmod +x /tmp/gpu_restore_state.sh
bash /tmp/gpu_restore_state.sh
`, containerName, checkpointDir)

	cmd := exec.Command("bash", "-c", restoreScript)
	output, err := cmd.CombinedOutput()
	if err != nil {
		result.Error = fmt.Sprintf("GPU恢复脚本执行失败: %v", err)
		return result, err
	}
	_ = output

	_ = NotifyGPUAppsRestore(containerName, processes)

	result.Success = true
	return result, nil
}

func applyGPUDeviceMapping(containerName string, mappings []GPUDeviceMapping) error {
	fmt.Printf("  应用GPU设备映射...\n")
	for _, m := range mappings {
		src := fmt.Sprintf("GPU%d", m.SourceGPUIndex)
		dst := fmt.Sprintf("GPU%d", m.TargetGPUIndex)
		if m.SourceMIGUUID != "" {
			src = m.SourceMIGUUID
		}
		if m.TargetMIGUUID != "" {
			dst = m.TargetMIGUUID
		}
		fmt.Printf("    映射: %s -> %s\n", src, dst)

		visibleDevices := fmt.Sprintf("NVIDIA_VISIBLE_DEVICES=%d", m.TargetGPUIndex)
		if m.TargetMIGUUID != "" {
			visibleDevices = fmt.Sprintf("NVIDIA_VISIBLE_DEVICES=%s", m.TargetMIGUUID)
		}

		cmd := exec.Command("lxc-attach", "-n", containerName, "--",
			"bash", "-c",
			fmt.Sprintf("export %s && echo $NVIDIA_VISIBLE_DEVICES", visibleDevices))

		output, err := cmd.CombinedOutput()
		if err != nil {
			fmt.Printf("    警告: 设置GPU映射失败: %v\n", err)
		} else {
			fmt.Printf("    已设置: %s\n", strings.TrimSpace(string(output)))
		}
	}

	return nil
}

func TestGPUAvailability(containerName string) error {
	fmt.Printf("  测试GPU可用性...\n")

	testScript := fmt.Sprintf(`
lxc-attach -n "%s" -- bash -c '
    if ! command -v nvidia-smi &> /dev/null; then
        echo "GPU_NOT_FOUND: nvidia-smi 不可用"
        exit 1
    fi

    if ! nvidia-smi -L &> /dev/null; then
        echo "GPU_NOT_FOUND: 无法访问GPU设备"
        exit 1
    fi

    nvidia-smi --query-gpu=index,name,memory.free --format=csv,noheader,nounits | head -1
    echo "GPU_OK"
'
`, containerName)

	cmd := exec.Command("bash", "-c", testScript)
	output, err := cmd.CombinedOutput()

	outputStr := string(output)
	if err != nil {
		if strings.Contains(outputStr, "GPU_NOT_FOUND") {
			return fmt.Errorf("目标主机无可用GPU: %s", outputStr)
		}
		return fmt.Errorf("GPU可用性测试失败: %v", err)
	}

	if strings.Contains(outputStr, "GPU_OK") {
		fmt.Println("    ✓ GPU可用")
		return nil
	}

	return fmt.Errorf("GPU测试结果异常: %s", outputStr)
}

func CheckTargetGPUAvailable(targetHost string, targetPort int) (*GPUState, error) {
	type gpuCheckRequest struct {
		Action string `json:"action"`
	}

	conn, err := net.DialTimeout("tcp", fmt.Sprintf("%s:%d", targetHost, targetPort), 10*time.Second)
	if err != nil {
		return nil, fmt.Errorf("连接目标主机检查GPU失败: %w", err)
	}
	defer conn.Close()

	req := gpuCheckRequest{Action: "check_gpu"}
	data, _ := json.Marshal(req)
	if _, err := conn.Write(data); err != nil {
		return nil, fmt.Errorf("发送GPU检查请求失败: %w", err)
	}

	buf := make([]byte, 8192)
	n, err := conn.Read(buf)
	if err != nil {
		return nil, fmt.Errorf("接收GPU检查响应失败: %w", err)
	}

	var state GPUState
	if err := json.Unmarshal(buf[:n], &state); err != nil {
		return nil, fmt.Errorf("解析GPU检查响应失败: %w", err)
	}

	return &state, nil
}

func ParseGPUMapping(mappingStr string) ([]GPUDeviceMapping, error) {
	if mappingStr == "" {
		return nil, nil
	}

	var mappings []GPUDeviceMapping
	parts := strings.Split(mappingStr, ",")

	re := regexp.MustCompile(`(\d+)(?::(\d+))?(?:->(\d+)(?::(\d+))?)?`)

	for _, part := range parts {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}

		if strings.Contains(part, "MIG-") || strings.Contains(part, "GPU-") {
			uuidParts := strings.SplitN(part, "->", 2)
			if len(uuidParts) == 2 {
				mappings = append(mappings, GPUDeviceMapping{
					SourceMIGUUID: strings.TrimSpace(uuidParts[0]),
					TargetMIGUUID: strings.TrimSpace(uuidParts[1]),
				})
			} else {
				return nil, fmt.Errorf("无效的MIG UUID映射格式: %s (正确格式: MIG-UUID-SRC->MIG-UUID-DST)", part)
			}
			continue
		}

		matches := re.FindStringSubmatch(part)
		if matches == nil {
			return nil, fmt.Errorf("无效的GPU映射格式: %s (正确格式: 0->1, 1->2)", part)
		}

		mapping := GPUDeviceMapping{}
		mapping.SourceGPUIndex, _ = strconv.Atoi(matches[1])
		if len(matches) >= 4 && matches[3] != "" {
			mapping.TargetGPUIndex, _ = strconv.Atoi(matches[3])
		} else {
			mapping.TargetGPUIndex = mapping.SourceGPUIndex
		}
		mappings = append(mappings, mapping)
	}

	return mappings, nil
}

func ValidateGPUMapping(sourceGPUs, targetGPUs *GPUState, mappings []GPUDeviceMapping) error {
	if len(mappings) == 0 {
		if sourceGPUs.HasGPU && !targetGPUs.HasGPU {
			return fmt.Errorf("源主机有GPU但目标主机无GPU，迁移取消。使用 --force 可强制迁移（不推荐）")
		}
		return nil
	}

	if !targetGPUs.HasGPU {
		return fmt.Errorf("指定了GPU映射但目标主机无GPU")
	}

	for _, m := range mappings {
		if m.SourceMIGUUID == "" && m.SourceGPUIndex >= len(sourceGPUs.GPUs) {
			return fmt.Errorf("源GPU索引 %d 超出范围 (源主机有 %d 个GPU)",
				m.SourceGPUIndex, len(sourceGPUs.GPUs))
		}
		if m.TargetMIGUUID == "" && m.TargetGPUIndex >= len(targetGPUs.GPUs) {
			return fmt.Errorf("目标GPU索引 %d 超出范围 (目标主机有 %d 个GPU)",
				m.TargetGPUIndex, len(targetGPUs.GPUs))
		}
	}

	return nil
}

func FormatGPUMapping(mappings []GPUDeviceMapping) string {
	if len(mappings) == 0 {
		return "无"
	}

	var parts []string
	for _, m := range mappings {
		src := fmt.Sprintf("GPU%d", m.SourceGPUIndex)
		dst := fmt.Sprintf("GPU%d", m.TargetGPUIndex)
		if m.SourceMIGUUID != "" {
			src = m.SourceMIGUUID
		}
		if m.TargetMIGUUID != "" {
			dst = m.TargetMIGUUID
		}
		parts = append(parts, fmt.Sprintf("%s->%s", src, dst))
	}
	return strings.Join(parts, ", ")
}
