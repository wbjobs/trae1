package checkpoint

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"time"

	"github.com/lxc-migrate/lxc-migrate/internal/types"
)

type CriuManager struct {
	criuPath  string
	lxcPath   string
	workDir   string
	verbose   bool
}

func NewCriuManager(workDir string, verbose bool) *CriuManager {
	criuPath, _ := exec.LookPath("criu")
	if criuPath == "" {
		criuPath = "/usr/sbin/criu"
	}
	lxcPath, _ := exec.LookPath("lxc-checkpoint")
	if lxcPath == "" {
		lxcPath = "/usr/bin/lxc-checkpoint"
	}
	return &CriuManager{
		criuPath: criuPath,
		lxcPath:  lxcPath,
		workDir:  workDir,
		verbose:  verbose,
	}
}

func (c *CriuManager) PreDump(containerName string, iterDir string, prevIterDir string) error {
	args := []string{
		"pre-dump",
		"--tcp-established",
		"--ext-unix-sk",
		"--shell-job",
		"--track-mem",
		"-D", iterDir,
	}

	if prevIterDir != "" {
		args = append(args, "--prev-images-dir", prevIterDir)
	}

	args = append(args, "dump", "--tree", c.getContainerPID(containerName))

	cmd := exec.Command(c.criuPath, args...)
	if c.verbose {
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
	}

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("pre-dump iteration failed: %w", err)
	}
	return nil
}

func (c *CriuManager) Dump(containerName string, dumpDir string, prevIterDir string) error {
	args := []string{
		"dump",
		"--tcp-established",
		"--ext-unix-sk",
		"--shell-job",
		"--track-mem",
		"-D", dumpDir,
	}

	if prevIterDir != "" {
		args = append(args, "--prev-images-dir", prevIterDir)
	}

	args = append(args, "--tree", c.getContainerPID(containerName))

	cmd := exec.Command(c.criuPath, args...)
	if c.verbose {
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
	}

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("final dump failed: %w", err)
	}
	return nil
}

func (c *CriuManager) Restore(containerName string, dumpDir string, newIP string) error {
	args := []string{
		"restore",
		"--tcp-established",
		"--ext-unix-sk",
		"--shell-job",
		"-D", dumpDir,
		"--restore-detached",
		"--log-file", filepath.Join(dumpDir, "restore.log"),
	}

	if newIP != "" {
		args = append(args, "--action-script", c.getActionScriptPath())
	}

	cmd := exec.Command(c.criuPath, args...)
	if c.verbose {
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
	}
	cmd.Dir = dumpDir

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("restore failed: %w", err)
	}

	restorePID, err := c.getRestorePID(dumpDir)
	if err == nil && restorePID > 0 {
		_ = c.notifyNetworkChange(containerName, restorePID, newIP)
	}

	return nil
}

func (c *CriuManager) LxcCheckpoint(containerName string, dumpDir string, stop bool) error {
	args := []string{
		"-n", containerName,
		"-D", dumpDir,
		"--stop", fmt.Sprintf("%t", stop),
		"--tcp-established",
	}

	cmd := exec.Command(c.lxcPath, args...)
	if c.verbose {
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
	}

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("lxc-checkpoint failed: %w", err)
	}
	return nil
}

func (c *CriuManager) LxcRestore(containerName string, dumpDir string) error {
	args := []string{
		"-n", containerName,
		"-D", dumpDir,
		"--restore",
	}

	cmd := exec.Command(c.lxcPath, args...)
	if c.verbose {
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
	}

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("lxc-restore failed: %w", err)
	}
	return nil
}

func (c *CriuManager) GetCheckpointMeta(dumpDir string, containerName string) (*types.CheckpointMeta, error) {
	pagesFile := filepath.Join(dumpDir, "pages-1.img")
	info, err := os.Stat(pagesFile)
	if err != nil {
		return nil, fmt.Errorf("cannot read pages file: %w", err)
	}

	meta := &types.CheckpointMeta{
		ContainerName: containerName,
		Timestamp:     time.Now(),
		MemorySize:    uint64(info.Size()),
		PagesCount:    uint64(info.Size() / 4096),
	}

	metaFile := filepath.Join(dumpDir, "stats-dump")
	if data, err := os.ReadFile(metaFile); err == nil {
		var stats map[string]interface{}
		if json.Unmarshal(data, &stats) == nil {
			if pages, ok := stats["pages_written"].(float64); ok {
				meta.PagesCount = uint64(pages)
			}
		}
	}

	return meta, nil
}

func (c *CriuManager) FreezeContainer(containerName string) error {
	freezePath, _ := exec.LookPath("lxc-freeze")
	if freezePath == "" {
		freezePath = "/usr/bin/lxc-freeze"
	}

	cmd := exec.Command(freezePath, "-n", containerName)
	if c.verbose {
		cmd.Stderr = os.Stderr
	}

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("freeze container %s failed: %w", containerName, err)
	}
	return nil
}

func (c *CriuManager) UnfreezeContainer(containerName string) error {
	unfreezePath, _ := exec.LookPath("lxc-unfreeze")
	if unfreezePath == "" {
		unfreezePath = "/usr/bin/lxc-unfreeze"
	}

	cmd := exec.Command(unfreezePath, "-n", containerName)
	if c.verbose {
		cmd.Stderr = os.Stderr
	}

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("unfreeze container %s failed: %w", containerName, err)
	}
	return nil
}

func (c *CriuManager) getContainerPID(containerName string) string {
	pidPath := fmt.Sprintf("/run/lxc/%s.pid", containerName)
	data, err := os.ReadFile(pidPath)
	if err != nil {
		return ""
	}
	return string(data)
}

func (c *CriuManager) getRestorePID(dumpDir string) (int, error) {
	pidFile := filepath.Join(dumpDir, "restore.pid")
	data, err := os.ReadFile(pidFile)
	if err != nil {
		return 0, err
	}
	var pid int
	_, err = fmt.Sscanf(string(data), "%d", &pid)
	return pid, err
}

func (c *CriuManager) getActionScriptPath() string {
	return filepath.Join(c.workDir, "scripts", "net-rewrite.sh")
}

func (c *CriuManager) notifyNetworkChange(containerName string, pid int, newIP string) error {
	if newIP == "" {
		return nil
	}
	notifyPath := filepath.Join(c.workDir, "scripts", "notify-net.sh")
	cmd := exec.Command(notifyPath, containerName, fmt.Sprintf("%d", pid), newIP)
	return cmd.Run()
}

func (c *CriuManager) CleanupDumpDir(dumpDir string) error {
	return os.RemoveAll(dumpDir)
}
