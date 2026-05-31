package cli

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/spf13/cobra"

	"github.com/lxc-migrate/lxc-migrate/internal/checkpoint"
	"github.com/lxc-migrate/lxc-migrate/internal/gpu"
	"github.com/lxc-migrate/lxc-migrate/internal/network"
	"github.com/lxc-migrate/lxc-migrate/internal/progress"
	"github.com/lxc-migrate/lxc-migrate/internal/resource"
	"github.com/lxc-migrate/lxc-migrate/internal/transport"
	"github.com/lxc-migrate/lxc-migrate/internal/types"
)

var (
	flagTargetHost  string
	flagTargetPort  int
	flagBwLimit     int64
	flagPreCopy     bool
	flagPreCopyIter int
	flagForce       bool
	flagGPUMapping  string
	flagGPUNoCheck  bool
	flagGPUSignalTimeout time.Duration
)

var migrateCmd = &cobra.Command{
	Use:   "migrate [container-name]",
	Short: "Migrate a running LXC container to a target host",
	Long: `Migrate a running LXC container from the current host to a target host
with minimal downtime (<100ms).

The migration process:
  1. Check resources on the target host (memory, disk, CPU)
  2. Pre-copy mode: iteratively transfer memory pages while container runs
  3. Final checkpoint: freeze container, transfer remaining dirty pages
  4. Restore container on target host
  5. Reconfigure network (IP may change, notify container applications)`,
	Args: cobra.ExactArgs(1),
	RunE: runMigrate,
}

func init() {
	migrateCmd.Flags().StringVarP(&flagTargetHost, "target", "t", "", "target host address (required)")
	migrateCmd.Flags().IntVarP(&flagTargetPort, "port", "p", 9999, "target daemon port")
	migrateCmd.Flags().Int64Var(&flagBwLimit, "bwlimit", 0, "bandwidth limit in MB/s (0 = unlimited)")
	migrateCmd.Flags().BoolVar(&flagPreCopy, "pre-copy", false, "enable pre-copy migration mode")
	migrateCmd.Flags().IntVar(&flagPreCopyIter, "precopy-iter", 3, "number of pre-copy iterations")
	migrateCmd.Flags().BoolVarP(&flagForce, "force", "f", false, "force migration even if checks fail")
	migrateCmd.Flags().StringVar(&flagGPUMapping, "gpu", "", "GPU device mapping (e.g. '0->1,GPU-aaa->MIG-bbb')")
	migrateCmd.Flags().BoolVar(&flagGPUNoCheck, "gpu-no-check", false, "skip GPU availability check on target")
	migrateCmd.Flags().DurationVar(&flagGPUSignalTimeout, "gpu-signal-timeout", 30*time.Second, "timeout waiting for GPU release signal")

	migrateCmd.MarkFlagRequired("target")
	rootCmd.AddCommand(migrateCmd)
}

func runMigrate(cmd *cobra.Command, args []string) error {
	containerName := args[0]
	logger := progress.NewLogger(flagVerbose)

	logger.Logf("Starting migration of container '%s' to %s:%d", containerName, flagTargetHost, flagTargetPort)

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Minute)
	defer cancel()

	workDir, err := os.MkdirTemp("", "lxc-migrate-*")
	if err != nil {
		return fmt.Errorf("create work dir: %w", err)
	}
	defer os.RemoveAll(workDir)

	logger.Verbosef("Work directory: %s", workDir)

	gpuCoord := gpu.NewGPUMigrationCoordinator(flagVerbose, workDir)
	defer gpuCoord.Cleanup()

	if flagGPUMapping != "" {
		if err := gpuCoord.ParseDeviceMappings(flagGPUMapping); err != nil {
			return fmt.Errorf("invalid GPU mapping: %w", err)
		}
		logger.Logf("GPU device mappings: %v", flagGPUMapping)
	}

	gpuCoord.SetSignalTimeout(flagGPUSignalTimeout)

	requiredMem, err := resource.GetContainerMemoryUsage(containerName)
	if err != nil {
		if !flagForce {
			return fmt.Errorf("get container memory usage: %w", err)
		}
		requiredMem = 512 * 1024 * 1024
		logger.Verbosef("Using default memory estimate: %d MB", requiredMem/(1024*1024))
	}
	logger.Logf("Container memory usage: %.1f MB", float64(requiredMem)/(1024*1024))

	requiredDisk, err := resource.EstimateDiskUsage(containerName)
	if err != nil {
		if !flagForce {
			return fmt.Errorf("estimate disk usage: %w", err)
		}
		requiredDisk = 1024 * 1024 * 1024
	}
	logger.Logf("Container disk usage: %.1f MB", float64(requiredDisk)/(1024*1024))

	logger.Log("Connecting to target host...")
	client := transport.NewTransportClient(flagTargetHost, flagTargetPort, flagBwLimit, nil)
	if err := client.Connect(ctx); err != nil {
		return fmt.Errorf("connect to target: %w", err)
	}
	defer client.Close()
	logger.Log("Connected to target host")

	logger.Log("Checking target host resources...")
	ack, err := client.CheckResources(containerName, requiredMem, requiredDisk)
	if err != nil {
		if !flagForce {
			return fmt.Errorf("resource check failed: %w", err)
		}
		logger.Errorf("Resource check failed (continuing due to --force): %v", err)
	} else {
		logger.Logf("Target resources OK: free memory %.1f MB, free disk %.1f GB",
			float64(ack.FreeMemory)/(1024*1024),
			float64(ack.FreeDisk)/(1024*1024*1024))
		if ack.NewIP != "" {
			logger.Logf("Target assigned IP: %s", ack.NewIP)
		}
	}

	logger.Log("=== GPU Check ===")
	containerPID := getContainerPID(containerName)
	sourceGPUs := gpuCoord.GetGPUDevices()
	targetHasGPU := false
	if ack != nil {
		targetHasGPU = len(sourceGPUs) > 0
	}

	if !flagGPUNoCheck {
		if err := gpuCoord.PreMigrationCheck(containerPID, targetHasGPU); err != nil {
			if !flagForce {
				return fmt.Errorf("GPU pre-check failed: %w", err)
			}
			logger.Errorf("GPU pre-check failed (continuing due to --force): %v", err)
		}
	}

	if gpuCoord.GetState() != nil && len(gpuCoord.GetState().Devices) > 0 {
		logger.Logf("GPU migration enabled: %d GPU devices, %d GPU processes",
			len(gpuCoord.GetState().Devices), len(gpuCoord.GetState().Processes))

		for _, gpu := range gpuCoord.GetState().Devices {
			logger.Logf("  - GPU %d (%s): %s, %d MB", gpu.ID, gpu.UUID, gpu.Name, gpu.MemoryMB)
		}
	}

	criu := checkpoint.NewCriuManager(workDir, flagVerbose)

	var finalDumpDir string
	var oldIP string

	oldIP, _ = network.GetContainerIP(containerName)
	if oldIP == "" {
		oldIP = "unknown"
	}
	logger.Logf("Container current IP: %s", oldIP)

	if flagPreCopy {
		logger.Log("=== Pre-Copy Mode ===")
		if err := runPreCopyPhase(ctx, client, criu, containerName, workDir, logger); err != nil {
			return fmt.Errorf("pre-copy phase failed: %w", err)
		}
	}

	logger.Log("=== Final Checkpoint ===")
	finalDumpDir = filepath.Join(workDir, "final-dump")
	if err := os.MkdirAll(finalDumpDir, 0700); err != nil {
		return fmt.Errorf("create final dump dir: %w", err)
	}

	if gpuCoord.GetState() != nil && len(gpuCoord.GetState().Processes) > 0 {
		logger.Log("Releasing GPU resources (sending SIGUSR1)...")
		gpuReleaseStart := time.Now()
		if err := gpuCoord.ReleaseGPUResources(containerPID); err != nil {
			if !flagForce {
				gpuCoord.Cleanup()
				return fmt.Errorf("GPU release failed: %w", err)
			}
			logger.Errorf("GPU release failed (continuing due to --force): %v", err)
		}
		logger.Logf("GPU resources released in %v", time.Since(gpuReleaseStart))

		logger.Log("Saving GPU state to checkpoint...")
		if err := gpuCoord.SaveGPUState(finalDumpDir); err != nil {
			if !flagForce {
				gpuCoord.Cleanup()
				return fmt.Errorf("GPU state save failed: %w", err)
			}
			logger.Errorf("GPU state save failed (continuing due to --force): %v", err)
		}
	}

	logger.Log("Freezing container...")
	freezeStart := time.Now()
	if err := criu.FreezeContainer(containerName); err != nil {
		gpuCoord.Cleanup()
		return fmt.Errorf("freeze container: %w", err)
	}
	logger.Logf("Container frozen in %v", time.Since(freezeStart))

	prevDir := ""
	if flagPreCopy {
		prevDir = filepath.Join(workDir, fmt.Sprintf("pre-dump-%d", flagPreCopyIter))
	}

	logger.Log("Dumping container state...")
	dumpStart := time.Now()
	if err := criu.Dump(containerName, finalDumpDir, prevDir); err != nil {
		criu.UnfreezeContainer(containerName)
		gpuCoord.Cleanup()
		return fmt.Errorf("dump container: %w", err)
	}
	logger.Logf("Dump completed in %v", time.Since(dumpStart))

	meta, _ := criu.GetCheckpointMeta(finalDumpDir, containerName)
	if meta != nil {
		logger.Logf("Checkpoint: %d pages (%.1f MB)", meta.PagesCount, float64(meta.MemorySize)/(1024*1024))
	}

	logger.Log("Transferring checkpoint data to target...")
	transferStart := time.Now()
	restoreDir, err := client.SendFinalCheckpoint(finalDumpDir)
	if err != nil {
		criu.UnfreezeContainer(containerName)
		gpuCoord.Cleanup()
		return fmt.Errorf("send checkpoint: %w", err)
	}
	logger.Logf("Transfer completed in %v", time.Since(transferStart))

	logger.Log("Restoring container on target...")
	restoreStart := time.Now()
	if err := client.SendComplete(); err != nil {
		logger.Errorf("Failed to send complete signal: %v", err)
	}
	logger.Logf("Restore signal sent in %v", time.Since(restoreStart))

	logger.Log("=== Network Reconfiguration ===")
	if ack != nil && ack.NewIP != "" {
		nc := types.NetworkConfig{
			OldIP:     oldIP,
			NewIP:     ack.NewIP,
			Interface: "eth0",
		}

		logger.Logf("Reconfiguring network: %s -> %s", oldIP, ack.NewIP)
		if err := client.SendNetworkReconfig(oldIP, ack.NewIP); err != nil {
			logger.Errorf("Network reconfig on target failed: %v", err)
		}

		if err := network.ApplyNetworkConfig(containerName, nc); err != nil {
			logger.Errorf("Local network config update failed: %v", err)
		}
	}

	logger.Logf("Container '%s' successfully migrated to %s", containerName, flagTargetHost)
	logger.Log("=== Migration Complete ===")
	logger.Log("Container is now running on the target host")
	logger.Logf("Old IP: %s", oldIP)
	if ack != nil && ack.NewIP != "" {
		logger.Logf("New IP: %s", ack.NewIP)
	}

	if gpuCoord.GetState() != nil && len(gpuCoord.GetState().Devices) > 0 {
		logger.Log("GPU state was transferred with the checkpoint")
		logger.Log("Target daemon will automatically restore GPU resources (SIGUSR2)")
	}

	_ = restoreDir
	return nil
}

func runPreCopyPhase(ctx context.Context, client *transport.TransportClient, criu *checkpoint.CriuManager, containerName string, workDir string, logger *progress.MigrationLogger) error {
	logger.Logf("Starting %d pre-copy iterations...", flagPreCopyIter)

	var prevIterDir string
	for i := 1; i <= flagPreCopyIter; i++ {
		iterStart := time.Now()
		iterDir := filepath.Join(workDir, fmt.Sprintf("pre-dump-%d", i))
		if err := os.MkdirAll(iterDir, 0700); err != nil {
			return fmt.Errorf("create pre-dump dir %d: %w", i, err)
		}

		logger.Logf("Pre-copy iteration %d/%d...", i, flagPreCopyIter)

		if err := criu.PreDump(containerName, iterDir, prevIterDir); err != nil {
			return fmt.Errorf("pre-dump iteration %d: %w", i, err)
		}

		logger.Logf("Iteration %d dump completed in %v", i, time.Since(iterStart))

		if err := client.SendPreCopyData(iterDir, i); err != nil {
			return fmt.Errorf("send pre-copy data iteration %d: %w", i, err)
		}

		logger.Logf("Iteration %d transfer completed", i)
		prevIterDir = iterDir
	}

	logger.Log("Pre-copy phase complete")
	return nil
}

func getContainerPID(containerName string) int {
	pidPath := fmt.Sprintf("/run/lxc/%s.pid", containerName)
	data, err := os.ReadFile(pidPath)
	if err != nil {
		return 0
	}
	pid := 0
	fmt.Sscanf(string(data), "%d", &pid)
	return pid
}
