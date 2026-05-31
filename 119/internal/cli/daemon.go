package cli

import (
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/spf13/cobra"

	"github.com/lxc-migrate/lxc-migrate/internal/gpu"
	"github.com/lxc-migrate/lxc-migrate/internal/transport"
	"github.com/lxc-migrate/lxc-migrate/internal/types"
)

var daemonCmd = &cobra.Command{
	Use:   "daemon",
	Short: "Run the migration daemon on the target host",
	Long: `Start the LXC migration daemon that listens for incoming container
migrations. Run this on the target host before initiating a migration.

The daemon will:
  - Listen for TCP connections from migration clients
  - Check available system resources before accepting a migration
  - Receive checkpoint data streams
  - Restore containers using CRIU
  - Reconfigure network settings after restore`,
	RunE: runDaemon,
}

func init() {
	daemonCmd.Flags().IntVarP(&flagDaemonPort, "port", "p", 9999, "TCP port to listen on")
	daemonCmd.Flags().StringVar(&flagDataDir, "data-dir", "/var/lib/lxc-migrate", "directory for checkpoint data")
	rootCmd.AddCommand(daemonCmd)
}

func runDaemon(cmd *cobra.Command, args []string) error {
	absDir, err := filepath.Abs(flagDataDir)
	if err != nil {
		return fmt.Errorf("resolve data dir: %w", err)
	}

	if err := os.MkdirAll(absDir, 0700); err != nil {
		return fmt.Errorf("create data dir: %w", err)
	}

	config := &types.DaemonConfig{
		ListenAddr: "0.0.0.0",
		ListenPort: flagDaemonPort,
		DataDir:    absDir,
		MaxConn:    5,
	}

	server := transport.NewTransportServer(config)

	server.OnRestore = func(dumpDir string, containerName string) error {
		return restoreContainer(dumpDir, containerName, flagVerbose)
	}

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		<-sigCh
		fmt.Println("\n[daemon] shutting down...")
		server.Stop()
	}()

	fmt.Printf("[daemon] LXC migration daemon starting...\n")
	fmt.Printf("[daemon] Listening on %s:%d\n", config.ListenAddr, config.ListenPort)
	fmt.Printf("[daemon] Data directory: %s\n", config.DataDir)
	fmt.Printf("[daemon] Press Ctrl+C to stop\n")

	if err := server.Start(); err != nil {
		return fmt.Errorf("daemon error: %w", err)
	}

	fmt.Println("[daemon] stopped")
	return nil
}

func restoreContainer(dumpDir string, containerName string, verbose bool) error {
	fmt.Printf("[daemon] === GPU Restore Check ===\n")

	gpuCoord := gpu.NewGPUMigrationCoordinator(verbose, dumpDir)
	defer gpuCoord.Cleanup()

	gpuCoord.SetSignalTimeout(30 * time.Second)

	targetGPUs := gpuCoord.GetGPUDevices()
	if len(targetGPUs) > 0 {
		fmt.Printf("[daemon] target has %d GPU(s):\n", len(targetGPUs))
		for _, gpu := range targetGPUs {
			fmt.Printf("[daemon]   - GPU %d: %s (UUID: %s, Memory: %d MB)\n",
				gpu.ID, gpu.Name, gpu.UUID, gpu.MemoryMB)
		}
	}

	if err := gpuCoord.LoadGPUState(dumpDir); err != nil {
		fmt.Fprintf(os.Stderr, "[daemon] warning: failed to load GPU state: %v\n", err)
	}

	if gpuCoord.GetState() != nil && len(gpuCoord.GetState().Devices) > 0 {
		fmt.Printf("[daemon] checkpoint contains GPU state: %d devices, %d processes\n",
			len(gpuCoord.GetState().Devices), len(gpuCoord.GetState().Processes))

		if len(targetGPUs) == 0 {
			return fmt.Errorf("checkpoint contains GPU state but target has no NVIDIA GPU available - migration cancelled")
		}

		if err := gpuCoord.VerifyGPU(targetGPUs); err != nil {
			return fmt.Errorf("target GPU verification failed: %w", err)
		}
		fmt.Println("[daemon] GPU verification passed")
	}

	criuPath, _ := exec.LookPath("criu")
	if criuPath == "" {
		criuPath = "/usr/sbin/criu"
	}

	args := []string{
		"restore",
		"--tcp-established",
		"--ext-unix-sk",
		"--shell-job",
		"-D", dumpDir,
		"--restore-detached",
		"--log-file", filepath.Join(dumpDir, "restore.log"),
	}

	cmd := exec.Command(criuPath, args...)
	if verbose {
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
	}
	cmd.Dir = dumpDir

	fmt.Printf("[daemon] restoring container '%s' using CRIU...\n", containerName)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("CRIU restore failed: %w", err)
	}
	fmt.Println("[daemon] CRIU restore completed")

	restorePID := getRestorePID(dumpDir)
	if restorePID <= 0 {
		restorePID = 1
	}

	lxcRestorePath, _ := exec.LookPath("lxc-checkpoint")
	if lxcRestorePath != "" {
		lxcCmd := exec.Command(lxcRestorePath, "-n", containerName, "-D", dumpDir, "--restore")
		if verbose {
			lxcCmd.Stdout = os.Stdout
			lxcCmd.Stderr = os.Stderr
		}
		if err := lxcCmd.Run(); err != nil {
			fmt.Fprintf(os.Stderr, "[daemon] warning: lxc-checkpoint restore failed: %v\n", err)
		}
	}

	if gpuCoord.GetState() != nil && len(gpuCoord.GetState().Processes) > 0 {
		fmt.Printf("[daemon] restoring GPU resources (sending SIGUSR2)...\n")
		if err := gpuCoord.RestoreGPUResources(restorePID, targetGPUs); err != nil {
			fmt.Fprintf(os.Stderr, "[daemon] warning: GPU restore failed: %v\n", err)
		} else {
			fmt.Println("[daemon] GPU resources restored successfully")
		}

		fmt.Println("[daemon] testing GPU availability...")
		gpuTestMgr := gpu.NewGPUManager(verbose)
		for _, gpu := range targetGPUs {
			if err := gpuTestMgr.TestGPUAvailability(gpu.UUID); err != nil {
				fmt.Fprintf(os.Stderr, "[daemon] warning: GPU %s test failed: %v\n", gpu.UUID, err)
			} else {
				fmt.Printf("[daemon] GPU %s test passed\n", gpu.UUID)
			}
		}
	}

	return nil
}

func getRestorePID(dumpDir string) int {
	pidFile := filepath.Join(dumpDir, "restore.pid")
	data, err := os.ReadFile(pidFile)
	if err != nil {
		return 0
	}
	pid := 0
	fmt.Sscanf(string(data), "%d", &pid)
	return pid
}
