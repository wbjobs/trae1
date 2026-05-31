package cli

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"

	"github.com/lxc-migrate/lxc-migrate/internal/gpu"
)

var gpuCmd = &cobra.Command{
	Use:   "gpu",
	Short: "GPU management commands",
	Long:  "Commands for managing NVIDIA GPU resources during container migration",
}

var gpuListCmd = &cobra.Command{
	Use:   "list",
	Short: "List available NVIDIA GPUs and MIG devices",
	Long: `List all available NVIDIA GPUs, their memory, UUID, and MIG status.
Also shows MIG (Multi-Instance GPU) devices if enabled.`,
	RunE: runGPUList,
}

var gpuTestCmd = &cobra.Command{
	Use:   "test",
	Short: "Test GPU availability and CUDA functionality",
	Long:  "Test if NVIDIA GPUs are accessible and CUDA is working properly",
	RunE: runGPUTest,
}

func init() {
	gpuCmd.AddCommand(gpuListCmd)
	gpuCmd.AddCommand(gpuTestCmd)
	rootCmd.AddCommand(gpuCmd)
}

func runGPUList(cmd *cobra.Command, args []string) error {
	coord := gpu.NewGPUMigrationCoordinator(flagVerbose, "")

	devices, err := coord.ListGPUs()
	if err != nil {
		return fmt.Errorf("list GPUs: %w", err)
	}

	if len(devices) == 0 {
		fmt.Println("No NVIDIA GPUs detected")
		return nil
	}

	fmt.Printf("Found %d NVIDIA GPU(s):\n", len(devices))
	fmt.Println("------------------------------------------------------------------")

	for _, dev := range devices {
		fmt.Printf("GPU %d:\n", dev.ID)
		fmt.Printf("  Name:        %s\n", dev.Name)
		fmt.Printf("  UUID:        %s\n", dev.UUID)
		fmt.Printf("  Memory:      %d MB (Free: %d MB)\n", dev.MemoryMB, dev.FreeMemoryMB)
		fmt.Printf("  Bus ID:      %s\n", dev.BusID)
		fmt.Printf("  MIG:         %s\n", dev.MIGMode)
		fmt.Printf("  Compute:     %s\n", dev.ComputeMode)

		if dev.MIGEnabled {
			fmt.Println("  MIG Devices:")
			migDevs, err := coord.ListMIGDevices()
			if err == nil && len(migDevs) > 0 {
				for _, mig := range migDevs {
					if mig.GPUID == dev.ID {
						fmt.Printf("    - GI %d/CI %d: %s (UUID: %s, %d MB)\n",
							mig.GIID, mig.CIID, mig.Profile, mig.UUID, mig.MemoryMB)
					}
				}
			}
		}
		fmt.Println("------------------------------------------------------------------")
	}

	return nil
}

func runGPUTest(cmd *cobra.Command, args []string) error {
	coord := gpu.NewGPUMigrationCoordinator(flagVerbose, "")

	devices, err := coord.ListGPUs()
	if err != nil {
		return fmt.Errorf("list GPUs: %w", err)
	}

	if len(devices) == 0 {
		fmt.Println("ERROR: No NVIDIA GPUs detected")
		os.Exit(1)
	}

	mgr := gpu.NewGPUManager(flagVerbose)

	if !mgr.IsAvailable() {
		fmt.Println("ERROR: nvidia-smi not found")
		os.Exit(1)
	}
	fmt.Println("✓ nvidia-smi is available")

	if !mgr.IsCUDAAvailable() {
		fmt.Println("⚠ CUDA toolkit not found (optional, required for GPU state save/restore)")
	} else {
		fmt.Println("✓ CUDA toolkit is available")
	}

	for _, dev := range devices {
		fmt.Printf("\nTesting GPU %d (%s)...\n", dev.ID, dev.UUID)

		if err := mgr.TestGPUAvailability(dev.UUID); err != nil {
			fmt.Printf("  ✗ FAILED: %v\n", err)
			os.Exit(1)
		} else {
			fmt.Printf("  ✓ GPU %d is accessible\n", dev.ID)
			fmt.Printf("    Memory: %d MB total, %d MB free\n", dev.MemoryMB, dev.FreeMemoryMB)
		}
	}

	fmt.Println("\n✓ All GPU tests passed")
	return nil
}
