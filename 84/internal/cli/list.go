package cli

import (
	"context"
	"fmt"
	"os"
	"text/tabwriter"
	"time"

	"github.com/spf13/cobra"
	"go.uber.org/zap"

	k8spkg "github.com/rbd-snap/ceph-rbd-snap/internal/k8s"
)

func NewListCmd(app *App) *cobra.Command {
	var (
		pvcName   string
		rbdImage  string
		showAll   bool
	)

	cmd := &cobra.Command{
		Use:   "list",
		Short: "List RBD snapshots",
		Long: `List snapshots for RBD images.
Can target a specific PVC or RBD image, or show all snapshots.`,
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
			defer cancel()

			if err := app.InitRBDClient(); err != nil {
				return err
			}
			defer app.RBDClient.Close()

			if showAll {
				return listAllSnapshots(ctx, app)
			}

			if pvcName == "" && rbdImage == "" {
				return fmt.Errorf("either --pvc, --rbd-image, or --all is required")
			}

			if pvcName != "" {
				return listSnapshotsForPVC(ctx, app, pvcName)
			}

			return listSnapshotsForImage(ctx, app, rbdImage)
		},
	}

	cmd.Flags().StringVar(&pvcName, "pvc", "", "List snapshots for a specific PVC")
	cmd.Flags().StringVar(&rbdImage, "rbd-image", "", "List snapshots for a specific RBD image")
	cmd.Flags().BoolVar(&showAll, "all", false, "List snapshots for all available RBD images")

	return cmd
}

func listSnapshotsForImage(ctx context.Context, app *App, imageName string) error {
	app.Logger.Info("Listing snapshots for image", zap.String("image", imageName))

	snapshots, err := app.RBDClient.ListSnapshots(imageName)
	if err != nil {
		return fmt.Errorf("failed to list snapshots: %w", err)
	}

	if len(snapshots) == 0 {
		fmt.Printf("No snapshots found for image: %s\n", imageName)
		return nil
	}

	fmt.Printf("Snapshots for RBD image: %s\n\n", imageName)
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 3, ' ', 0)
	fmt.Fprintln(w, "SNAPSHOT NAME\tID\tSIZE\tCREATED AT\tPROTECTED")

	for _, snap := range snapshots {
		protected := "No"
		if snap.IsProtected {
			protected = "Yes"
		}
		fmt.Fprintf(w, "%s\t%d\t%s\t%s\t%s\n",
			snap.Name,
			snap.ID,
			formatBytes(int64(snap.Size)),
			snap.CreatedAt.Format(time.RFC3339),
			protected,
		)
	}
	w.Flush()

	return nil
}

func k8sClient(app *App) (*k8spkg.K8sClient, error) {
	return k8spkg.NewK8sClient(&k8spkg.K8sConfig{
		Kubeconfig: app.Viper.GetString("kubeconfig"),
		Namespace:  app.Viper.GetString("namespace"),
	}, app.Logger)
}

func listSnapshotsForPVC(ctx context.Context, app *App, pvcName string) error {
	app.Logger.Info("Listing snapshots for PVC", zap.String("pvc", pvcName))

	k8sClient, err := k8sClient(app)
	if err != nil {
		return err
	}

	pvc, err := k8sClient.GetPVC(ctx, app.Viper.GetString("namespace"), pvcName)
	if err != nil {
		return fmt.Errorf("failed to get PVC: %w", err)
	}

	if pvc.Spec.VolumeName == "" {
		return fmt.Errorf("PVC %s is not bound to any PV", pvcName)
	}

	pv, err := k8sClient.GetPV(ctx, pvc.Spec.VolumeName)
	if err != nil {
		return fmt.Errorf("failed to get PV: %w", err)
	}

	rbdInfo, err := k8sClient.ExtractRBDInfoFromPV(pv)
	if err != nil {
		return err
	}

	fmt.Printf("PVC: %s/%s\n", pvc.Namespace, pvc.Name)
	fmt.Printf("RBD Image: %s/%s\n\n", rbdInfo.Pool, rbdInfo.Image)

	return listSnapshotsForImage(ctx, app, rbdInfo.Image)
}

func listAllSnapshots(ctx context.Context, app *App) error {
	app.Logger.Info("Listing all snapshots")

	k8sClient, err := k8sClient(app)
	if err != nil {
		return err
	}

	namespace := app.Viper.GetString("namespace")
	pvcs, err := k8sClient.ListPVCs(ctx, namespace)
	if err != nil {
		app.Logger.Warn("Failed to list PVCs, falling back to direct RBD listing", zap.Error(err))
	}

	fmt.Println("=== RBD Snapshot Summary ===\n")

	w := tabwriter.NewWriter(os.Stdout, 0, 0, 3, ' ', 0)
	fmt.Fprintln(w, "PVC\tRBD IMAGE\tSNAPSHOTS COUNT")
	fmt.Fprintln(w, "---\t---\t---")

	totalSnapshots := 0

	for _, pvc := range pvcs {
		if pvc.Spec.VolumeName == "" {
			continue
		}

		pv, err := k8sClient.GetPV(ctx, pvc.Spec.VolumeName)
		if err != nil {
			continue
		}

		rbdInfo, err := k8sClient.ExtractRBDInfoFromPV(pv)
		if err != nil {
			continue
		}

		snapshots, err := app.RBDClient.ListSnapshots(rbdInfo.Image)
		if err != nil {
			app.Logger.Warn("Failed to list snapshots", zap.String("image", rbdInfo.Image), zap.Error(err))
			continue
		}

		fmt.Fprintf(w, "%s/%s\t%s\t%d\n",
			pvc.Namespace,
			pvc.Name,
			rbdInfo.Image,
			len(snapshots),
		)
		totalSnapshots += len(snapshots)
	}

	w.Flush()
	fmt.Printf("\nTotal snapshots: %d\n", totalSnapshots)

	return nil
}

func formatBytes(bytes int64) string {
	const unit = 1024
	if bytes < unit {
		return fmt.Sprintf("%d B", bytes)
	}
	div, exp := int64(unit), 0
	for n := bytes / unit; n >= unit; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %ciB", float64(bytes)/float64(div), "KMGTPE"[exp])
}

func NewDeleteCmd(app *App) *cobra.Command {
	var (
		rbdImage     string
		pvcName      string
		snapshotName string
		allForImage  bool
		force        bool
	)

	cmd := &cobra.Command{
		Use:   "delete",
		Short: "Delete RBD snapshots",
		Long: `Delete one or more RBD snapshots.
Can delete a specific snapshot, all snapshots for an image, or match by criteria.`,
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Minute)
			defer cancel()

			if err := app.InitRBDClient(); err != nil {
				return err
			}
			defer app.RBDClient.Close()

			if pvcName != "" && rbdImage == "" {
				k8sClient, err := k8sClient(app)
				if err != nil {
					return err
				}

				pvc, err := k8sClient.GetPVC(ctx, app.Viper.GetString("namespace"), pvcName)
				if err != nil {
					return err
				}
				pv, err := k8sClient.GetPV(ctx, pvc.Spec.VolumeName)
				if err != nil {
					return err
				}
				rbdInfo, err := k8sClient.ExtractRBDInfoFromPV(pv)
				if err != nil {
					return err
				}
				rbdImage = rbdInfo.Image
			}

			if rbdImage == "" {
				return fmt.Errorf("either --rbd-image or --pvc is required")
			}

			if allForImage {
				return deleteAllSnapshotsForImage(ctx, app, rbdImage, force)
			}

			if snapshotName == "" {
				return fmt.Errorf("--snapshot is required unless --all is specified")
			}

			app.Logger.Info("Deleting snapshot",
				zap.String("image", rbdImage),
				zap.String("snapshot", snapshotName),
			)

			if !force {
				fmt.Printf("Are you sure you want to delete snapshot %s@%s? (yes/no): ", rbdImage, snapshotName)
				var confirm string
				fmt.Scanln(&confirm)
				if confirm != "yes" {
					fmt.Println("Operation cancelled")
					return nil
				}
			}

			err := app.RBDClient.UnprotectSnapshot(rbdImage, snapshotName)
			if err != nil {
				app.Logger.Warn("Failed to unprotect snapshot, trying delete anyway", zap.Error(err))
			}

			err = app.RBDClient.DeleteSnapshot(rbdImage, snapshotName)
			if err != nil {
				return fmt.Errorf("failed to delete snapshot: %w", err)
			}

			fmt.Printf("Deleted snapshot: %s@%s\n", rbdImage, snapshotName)
			return nil
		},
	}

	cmd.Flags().StringVar(&rbdImage, "rbd-image", "", "RBD image name")
	cmd.Flags().StringVar(&pvcName, "pvc", "", "PVC name (alternative to --rbd-image)")
	cmd.Flags().StringVar(&snapshotName, "snapshot", "", "Snapshot name to delete")
	cmd.Flags().BoolVar(&allForImage, "all", false, "Delete all snapshots for the image")
	cmd.Flags().BoolVar(&force, "force", false, "Skip confirmation prompt")

	return cmd
}

func deleteAllSnapshotsForImage(ctx context.Context, app *App, imageName string, force bool) error {
	app.Logger.Info("Deleting all snapshots for image", zap.String("image", imageName))

	snapshots, err := app.RBDClient.ListSnapshots(imageName)
	if err != nil {
		return fmt.Errorf("failed to list snapshots: %w", err)
	}

	if len(snapshots) == 0 {
		fmt.Printf("No snapshots to delete for image: %s\n", imageName)
		return nil
	}

	if !force {
		fmt.Printf("Are you sure you want to delete ALL %d snapshots for image %s? (yes/no): ",
			len(snapshots), imageName)
		var confirm string
		fmt.Scanln(&confirm)
		if confirm != "yes" {
			fmt.Println("Operation cancelled")
			return nil
		}
	}

	deleted := 0
	for _, snap := range snapshots {
		err := app.RBDClient.UnprotectSnapshot(imageName, snap.Name)
		if err != nil {
			app.Logger.Warn("Failed to unprotect snapshot",
				zap.String("snapshot", snap.Name),
				zap.Error(err),
			)
		}

		err = app.RBDClient.DeleteSnapshot(imageName, snap.Name)
		if err != nil {
			app.Logger.Error("Failed to delete snapshot",
				zap.String("snapshot", snap.Name),
				zap.Error(err),
			)
			continue
		}
		deleted++
		fmt.Printf("Deleted: %s@%s\n", imageName, snap.Name)
	}

	fmt.Printf("\nDeleted %d/%d snapshots\n", deleted, len(snapshots))
	return nil
}
