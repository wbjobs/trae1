package cli

import (
	"context"
	"fmt"
	"strings"
	"time"

	"github.com/spf13/cobra"
	"go.uber.org/zap"
	"k8s.io/apimachinery/pkg/api/resource"

	cephpkg "github.com/rbd-snap/ceph-rbd-snap/internal/ceph"
	"github.com/rbd-snap/ceph-rbd-snap/internal/database"
	k8spkg "github.com/rbd-snap/ceph-rbd-snap/internal/k8s"
)

func NewCreateCmd(app *App) *cobra.Command {
	var (
		pvcNames        []string
		rbdImages       []string
		snapshotName    string
		prefix          string
		dbType          string
		dbHost          string
		dbPort          int
		dbUser          string
		dbPassword      string
		dbName          string
		dbPodName       string
		consistency     bool
		incremental     bool
		parentSnapshot  string
		quickFreeze     bool
		fullFreeze      bool
	)

	cmd := &cobra.Command{
		Use:   "create",
		Short: "Create RBD snapshots",
		Long: `Create snapshots for RBD images backing Kubernetes PVCs.

QUICK FREEZE MODE (default, recommended for large databases > 100GB):
  1. Creates RBD snapshot first (milliseconds, COW-based)
  2. Briefly freezes database only to flush logs (< 1 second)
  3. Unfreezes immediately
  4. Uses database crash recovery for consistency

FULL FREEZE MODE (use for critical consistency guarantees):
  1. Freezes database with full read lock
  2. Creates RBD snapshot
  3. Unfreezes database

The quick freeze mode leverages RBD's Copy-On-Write snapshot特性.
The snapshot captures the exact state at creation time. The brief freeze
only ensures transaction logs are flushed to disk. On restore, the
database performs crash recovery (like after power failure) to reach
a consistent state. This reduces application impact from 30+ seconds
to < 1 second.`,
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx, cancel := context.WithTimeout(context.Background(), 30*time.Minute)
			defer cancel()

			if err := app.InitRBDClient(); err != nil {
				return err
			}
			defer app.RBDClient.Close()

			k8sClient, err := k8spkg.NewK8sClient(&k8spkg.K8sConfig{
				Kubeconfig: app.Viper.GetString("kubeconfig"),
				Namespace:  app.Viper.GetString("namespace"),
			}, app.Logger)
			if err != nil {
				return fmt.Errorf("failed to create K8s client: %w", err)
			}

			targetImages, err := resolveRBDImages(ctx, k8sClient, pvcNames, rbdImages, app.Viper.GetString("namespace"))
			if err != nil {
				return err
			}

			if len(targetImages) == 0 {
				return fmt.Errorf("no RBD images specified")
			}

			useQuickFreeze := quickFreeze || !fullFreeze

			app.Logger.Info("Target RBD images for snapshot",
				zap.Strings("images", targetImages),
				zap.Bool("consistency", consistency),
				zap.Bool("quick_freeze", useQuickFreeze),
			)

			if useQuickFreeze && dbType != "" {
				return createSnapshotWithQuickFreeze(ctx, app, k8sClient, targetImages, snapshotName, prefix,
					dbType, dbHost, dbPort, dbUser, dbPassword, dbName, dbPodName, consistency, incremental, parentSnapshot)
			}

			return createSnapshotWithFullFreeze(ctx, app, k8sClient, targetImages, snapshotName, prefix,
				dbType, dbHost, dbPort, dbUser, dbPassword, dbName, dbPodName, consistency, incremental, parentSnapshot)
		},
	}

	cmd.Flags().StringSliceVar(&pvcNames, "pvc", []string{}, "Kubernetes PVC names to snapshot")
	cmd.Flags().StringSliceVar(&rbdImages, "rbd-image", []string{}, "Direct RBD image names to snapshot")
	cmd.Flags().StringVar(&snapshotName, "name", "", "Snapshot name (default: auto-generated with prefix)")
	cmd.Flags().StringVar(&prefix, "prefix", "rbd-snap", "Prefix for auto-generated snapshot names")
	cmd.Flags().StringVar(&dbType, "db-type", "", "Database type: mysql or postgresql")
	cmd.Flags().StringVar(&dbHost, "db-host", "localhost", "Database host")
	cmd.Flags().IntVar(&dbPort, "db-port", 3306, "Database port")
	cmd.Flags().StringVar(&dbUser, "db-user", "root", "Database user")
	cmd.Flags().StringVar(&dbPassword, "db-password", "", "Database password")
	cmd.Flags().StringVar(&dbName, "db-name", "", "Database name")
	cmd.Flags().StringVar(&dbPodName, "db-pod", "", "Database pod name in Kubernetes")
	cmd.Flags().BoolVar(&consistency, "consistency", false, "Create consistency group snapshot for all images")
	cmd.Flags().BoolVar(&incremental, "incremental", false, "Create incremental snapshot (diff-based)")
	cmd.Flags().StringVar(&parentSnapshot, "parent-snapshot", "", "Parent snapshot for incremental snapshots")
	cmd.Flags().BoolVar(&quickFreeze, "quick-freeze", true, "Use quick freeze mode (default: true) - RBD snapshot first, then brief log flush")
	cmd.Flags().BoolVar(&fullFreeze, "full-freeze", false, "Use full freeze mode - lock database before snapshot")

	return cmd
}

func resolveRBDImages(ctx context.Context, k8sClient *k8spkg.K8sClient, pvcNames, rbdImages []string, namespace string) ([]string, error) {
	var images []string
	images = append(images, rbdImages...)

	for _, pvcName := range pvcNames {
		pvc, err := k8sClient.GetPVC(ctx, namespace, pvcName)
		if err != nil {
			return nil, fmt.Errorf("failed to get PVC %s: %w", pvcName, err)
		}

		if pvc.Spec.VolumeName == "" {
			return nil, fmt.Errorf("PVC %s is not bound to any PV", pvcName)
		}

		pv, err := k8sClient.GetPV(ctx, pvc.Spec.VolumeName)
		if err != nil {
			return nil, fmt.Errorf("failed to get PV %s: %w", pvc.Spec.VolumeName, err)
		}

		rbdInfo, err := k8sClient.ExtractRBDInfoFromPV(pv)
		if err != nil {
			return nil, err
		}

		images = append(images, rbdInfo.Image)
	}

	return images, nil
}

func createSnapshotWithQuickFreeze(
	ctx context.Context,
	app *App,
	k8sClient *k8spkg.K8sClient,
	targetImages []string,
	snapshotName, prefix string,
	dbType, dbHost string,
	dbPort int,
	dbUser, dbPassword, dbName, dbPodName string,
	consistency, incremental bool,
	parentSnapshot string,
) error {
	snapName := snapshotName
	if snapName == "" {
		snapName = generateSnapshotName(prefix)
	}

	app.Logger.Info("=== QUICK FREEZE MODE: Creating RBD snapshot first, then brief database freeze ===")

	app.Logger.Info("Step 1: Creating RBD snapshots (COW, milliseconds)",
		zap.Strings("images", targetImages),
	)

	createdSnapshots := make(map[string]string)
	startTime := time.Now()

	for _, image := range targetImages {
		err := app.RBDClient.CreateSnapshot(image, snapName)
		if err != nil {
			app.Logger.Error("Failed to create RBD snapshot",
				zap.String("image", image),
				zap.Error(err),
			)
			for img, snap := range createdSnapshots {
				if delErr := app.RBDClient.DeleteSnapshot(img, snap); delErr != nil {
					app.Logger.Error("Failed to rollback snapshot",
						zap.String("image", img),
						zap.Error(delErr),
					)
				}
			}
			return fmt.Errorf("RBD snapshot creation failed for image %s: %w", image, err)
		}
		createdSnapshots[image] = snapName

		err = app.RBDClient.ProtectSnapshot(image, snapName)
		if err != nil {
			app.Logger.Warn("Failed to protect snapshot",
				zap.String("image", image),
				zap.String("snapshot", snapName),
				zap.Error(err),
			)
		}
	}

	snapshotDuration := time.Since(startTime)
	app.Logger.Info("Step 1 complete: RBD snapshots created",
		zap.Duration("duration", snapshotDuration),
		zap.Int("count", len(createdSnapshots)),
	)

	app.Logger.Info("Step 2: Quick freeze database - flush logs only (no table lock)")

	freezeStartTime := time.Now()
	var unfreezeFunc func()

	switch strings.ToLower(dbType) {
	case "mysql":
		cfg := &database.MySQLConfig{
			Host:     dbHost,
			Port:     dbPort,
			User:     dbUser,
			Password: dbPassword,
			Database: dbName,
			Timeout:  5 * time.Second,
		}

		mysqlClient, err := database.NewMySQLClient(cfg, app.Logger)
		if err != nil {
			app.Logger.Warn("Quick freeze: failed to connect to MySQL, but RBD snapshot is already created",
				zap.Error(err),
			)
			break
		}

		if err := mysqlClient.FreezeQuick(ctx); err != nil {
			app.Logger.Warn("Quick freeze: MySQL FLUSH LOGS failed", zap.Error(err))
		}

		unfreezeFunc = func() {
			mysqlClient.Close()
		}

	case "postgresql", "postgres":
		cfg := &database.PostgreSQLConfig{
			Host:     dbHost,
			Port:     dbPort,
			User:     dbUser,
			Password: dbPassword,
			Database: dbName,
			SSLMode:  "prefer",
			Timeout:  5 * time.Second,
		}

		pgClient, err := database.NewPostgreSQLClient(cfg, app.Logger)
		if err != nil {
			app.Logger.Warn("Quick freeze: failed to connect to PostgreSQL, but RBD snapshot is already created",
				zap.Error(err),
			)
			break
		}

		if _, err := pgClient.FreezeQuick(ctx); err != nil {
			app.Logger.Warn("Quick freeze: PostgreSQL checkpoint failed", zap.Error(err))
		}

		unfreezeFunc = func() {
			pgClient.Close()
		}

	default:
		app.Logger.Warn("Unknown database type for quick freeze",
			zap.String("db_type", dbType),
		)
	}

	freezeDuration := time.Since(freezeStartTime)

	if unfreezeFunc != nil {
		app.Logger.Info("Step 3: Releasing database connection")
		unfreezeFunc()
	}

	app.Logger.Info("=== Quick freeze snapshot completed ===")
	app.Logger.Info("Timing summary",
		zap.Duration("rbd_snapshot_duration", snapshotDuration),
		zap.Duration("db_freeze_duration", freezeDuration),
		zap.Duration("total_duration", time.Since(startTime)),
	)

	fmt.Printf("\n✅ Snapshot created successfully (Quick Freeze Mode)\n")
	fmt.Printf("   RBD Snapshot Time: %v\n", snapshotDuration)
	fmt.Printf("   DB Freeze Window:   %v (should be < 1s)\n", freezeDuration)
	fmt.Printf("   Snapshots:\n")
	for image, snap := range createdSnapshots {
		fmt.Printf("     %-40s @ %s\n", image, snap)
	}
	fmt.Printf("\n📝 Note: On restore, database will perform crash recovery automatically\n")
	fmt.Printf("   MySQL: InnoDB crash recovery (like power failure)\n")
	fmt.Printf("   PostgreSQL: WAL replay to consistent state\n")

	return nil
}

func createSnapshotWithFullFreeze(
	ctx context.Context,
	app *App,
	k8sClient *k8spkg.K8sClient,
	targetImages []string,
	snapshotName, prefix string,
	dbType, dbHost string,
	dbPort int,
	dbUser, dbPassword, dbName, dbPodName string,
	consistency, incremental bool,
	parentSnapshot string,
) error {
	app.Logger.Info("=== FULL FREEZE MODE: Lock database before snapshot ===")

	var unfreezeFunc func()
	var err error

	if dbType != "" {
		app.Logger.Info("Step 1: Full freeze database (FLUSH TABLES WITH READ LOCK / pg_start_backup)")
		unfreezeFunc, err = freezeDatabase(ctx, dbType, dbHost, dbPort, dbUser, dbPassword, dbName, dbPodName, k8sClient, app)
		if err != nil {
			return fmt.Errorf("failed to freeze database: %w", err)
		}
		defer func() {
			if unfreezeFunc != nil {
				app.Logger.Info("Step 3: Unfreezing database")
				unfreezeFunc()
			}
		}()
	}

	app.Logger.Info("Step 2: Creating RBD snapshots while database is locked")

	snapName := snapshotName
	if snapName == "" {
		snapName = generateSnapshotName(prefix)
	}

	if consistency {
		group := &cephpkg.ConsistencyGroup{
			Name:   prefix + "-cg",
			Images: targetImages,
		}

		snapPrefix := prefix
		if snapPrefix == "" {
			snapPrefix = "consistency-snap"
		}

		err = app.RBDClient.CreateConsistencySnapshot(group, snapPrefix)
		if err != nil {
			app.Logger.Error("Consistency snapshot failed", zap.Error(err))
			return err
		}

		fmt.Println("Consistency group snapshot created successfully:")
		for image, snap := range group.Snapshots {
			fmt.Printf("  Image: %-40s Snapshot: %s\n", image, snap)
		}
	} else {
		for _, image := range targetImages {
			err := app.RBDClient.CreateSnapshot(image, snapName)
			if err != nil {
				app.Logger.Error("Failed to create snapshot",
					zap.String("image", image),
					zap.Error(err),
				)
				return err
			}

			err = app.RBDClient.ProtectSnapshot(image, snapName)
			if err != nil {
				app.Logger.Warn("Failed to protect snapshot",
					zap.String("image", image),
					zap.String("snapshot", snapName),
					zap.Error(err),
				)
			}

			fmt.Printf("Snapshot created: %s@%s\n", image, snapName)
		}
	}

	app.Logger.Info("Snapshot creation completed successfully (full freeze mode)")
	return nil
}

func freezeDatabase(ctx context.Context, dbType, dbHost string, dbPort int, dbUser, dbPassword, dbName, dbPodName string, k8sClient *k8spkg.K8sClient, app *App) (func(), error) {
	switch strings.ToLower(dbType) {
	case "mysql":
		cfg := &database.MySQLConfig{
			Host:     dbHost,
			Port:     dbPort,
			User:     dbUser,
			Password: dbPassword,
			Database: dbName,
			Timeout:  30 * time.Second,
		}

		mysqlClient, err := database.NewMySQLClient(cfg, app.Logger)
		if err != nil {
			return nil, fmt.Errorf("failed to connect to MySQL: %w", err)
		}

		if err := mysqlClient.Freeze(ctx); err != nil {
			mysqlClient.Close()
			return nil, err
		}

		return func() {
			unfreezeCtx := context.Background()
			if err := mysqlClient.Unfreeze(unfreezeCtx); err != nil {
				app.Logger.Error("Failed to unfreeze MySQL", zap.Error(err))
			}
			mysqlClient.Close()
		}, nil

	case "postgresql", "postgres":
		cfg := &database.PostgreSQLConfig{
			Host:     dbHost,
			Port:     dbPort,
			User:     dbUser,
			Password: dbPassword,
			Database: dbName,
			SSLMode:  "prefer",
			Timeout:  30 * time.Second,
		}

		pgClient, err := database.NewPostgreSQLClient(cfg, app.Logger)
		if err != nil {
			return nil, fmt.Errorf("failed to connect to PostgreSQL: %w", err)
		}

		_, err = pgClient.Freeze(ctx)
		if err != nil {
			pgClient.Close()
			return nil, err
		}

		return func() {
			unfreezeCtx := context.Background()
			if err := pgClient.Unfreeze(unfreezeCtx); err != nil {
				app.Logger.Error("Failed to unfreeze PostgreSQL", zap.Error(err))
			}
			pgClient.Close()
		}, nil

	default:
		return nil, fmt.Errorf("unsupported database type: %s", dbType)
	}
}

func NewRestoreCmd(app *App) *cobra.Command {
	var (
		snapshotName   string
		sourceImage    string
		sourcePVC      string
		newPVCName     string
		newImageName   string
		storageClass   string
		verify         bool
		dbType         string
		dbHost         string
		dbPort         int
		dbUser         string
		dbPassword     string
		dbName         string
		cloneSize      string
	)

	cmd := &cobra.Command{
		Use:   "restore",
		Short: "Restore snapshot to new RBD image and PVC",
		Long: `Restore an RBD snapshot to a new image and create a Kubernetes PVC for it.
Optionally run database integrity checks after restoration.`,
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx, cancel := context.WithTimeout(context.Background(), 30*time.Minute)
			defer cancel()

			if err := app.InitRBDClient(); err != nil {
				return err
			}
			defer app.RBDClient.Close()

			k8sClient, err := k8spkg.NewK8sClient(&k8spkg.K8sConfig{
				Kubeconfig: app.Viper.GetString("kubeconfig"),
				Namespace:  app.Viper.GetString("namespace"),
			}, app.Logger)
			if err != nil {
				return fmt.Errorf("failed to create K8s client: %w", err)
			}

			if sourcePVC != "" && sourceImage == "" {
				pvc, err := k8sClient.GetPVC(ctx, app.Viper.GetString("namespace"), sourcePVC)
				if err != nil {
					return fmt.Errorf("failed to get PVC %s: %w", sourcePVC, err)
				}
				pv, err := k8sClient.GetPV(ctx, pvc.Spec.VolumeName)
				if err != nil {
					return fmt.Errorf("failed to get PV: %w", err)
				}
				rbdInfo, err := k8sClient.ExtractRBDInfoFromPV(pv)
				if err != nil {
					return err
				}
				sourceImage = rbdInfo.Image
			}

			if sourceImage == "" || snapshotName == "" {
				return fmt.Errorf("both --source-image (or --source-pvc) and --snapshot are required")
			}

			if newImageName == "" {
				newImageName = fmt.Sprintf("%s-restore-%s", sourceImage, snapshotName[:8])
			}
			if newPVCName == "" {
				newPVCName = generateRestorePVCName(sourceImage, snapshotName)
			}

			app.Logger.Info("Restoring snapshot",
				zap.String("source_image", sourceImage),
				zap.String("snapshot", snapshotName),
				zap.String("new_image", newImageName),
				zap.String("new_pvc", newPVCName),
			)

			var size uint64
			if cloneSize != "" {
				qty, err := resource.ParseQuantity(cloneSize)
				if err != nil {
					return fmt.Errorf("invalid clone size: %w", err)
				}
				sizeInBytes, ok := qty.AsInt64()
				if !ok {
					return fmt.Errorf("invalid size quantity")
				}
				size = uint64(sizeInBytes)
			}

			err = app.RBDClient.CloneImage(sourceImage, snapshotName, newImageName, size)
			if err != nil {
				return fmt.Errorf("failed to clone image: %w", err)
			}

			fmt.Printf("Cloned image: %s@%s -> %s\n", sourceImage, snapshotName, newImageName)

			imgInfo, err := app.RBDClient.GetImageInfo(newImageName)
			if err != nil {
				app.Logger.Warn("Failed to get image info", zap.Error(err))
			}

			pvcSize := resource.MustParse("10Gi")
			if imgInfo != nil {
				pvcSize = *resource.NewQuantity(int64(imgInfo.Size), resource.BinarySI)
			}

			createdPVC, err := k8sClient.CreatePVCFromRBD(
				ctx,
				app.Viper.GetString("namespace"),
				newPVCName,
				newImageName,
				app.Viper.GetString("ceph.pool"),
				storageClass,
				pvcSize,
			)
			if err != nil {
				return fmt.Errorf("failed to create PVC: %w", err)
			}

			fmt.Printf("Created PVC: %s/%s\n", createdPVC.Namespace, createdPVC.Name)

			if verify && dbType != "" {
				app.Logger.Info("Waiting for PVC to be ready before verification")

				err = k8sClient.WaitForPVCReady(ctx, app.Viper.GetString("namespace"), newPVCName, 5*time.Minute)
				if err != nil {
					app.Logger.Warn("Timeout waiting for PVC, proceeding with verification anyway", zap.Error(err))
				}

				app.Logger.Info("Running database verification")
				err = runDatabaseVerification(ctx, dbType, dbHost, dbPort, dbUser, dbPassword, dbName, app)
				if err != nil {
					app.Logger.Error("Database verification failed", zap.Error(err))
					return err
				}
				fmt.Println("Database verification completed successfully")
			}

			app.Logger.Info("Restore completed successfully")
			return nil
		},
	}

	cmd.Flags().StringVar(&snapshotName, "snapshot", "", "Snapshot name to restore")
	cmd.Flags().StringVar(&sourceImage, "source-image", "", "Source RBD image name")
	cmd.Flags().StringVar(&sourcePVC, "source-pvc", "", "Source PVC name (alternative to --source-image)")
	cmd.Flags().StringVar(&newImageName, "new-image", "", "New RBD image name (default: auto-generated)")
	cmd.Flags().StringVar(&newPVCName, "new-pvc", "", "New PVC name (default: auto-generated)")
	cmd.Flags().StringVar(&storageClass, "storage-class", "", "Storage class name for the new PVC")
	cmd.Flags().BoolVar(&verify, "verify", false, "Run database integrity check after restore")
	cmd.Flags().StringVar(&dbType, "db-type", "", "Database type for verification: mysql or postgresql")
	cmd.Flags().StringVar(&dbHost, "db-host", "localhost", "Database host for verification")
	cmd.Flags().IntVar(&dbPort, "db-port", 3306, "Database port for verification")
	cmd.Flags().StringVar(&dbUser, "db-user", "root", "Database user for verification")
	cmd.Flags().StringVar(&dbPassword, "db-password", "", "Database password for verification")
	cmd.Flags().StringVar(&dbName, "db-name", "", "Database name for verification")
	cmd.Flags().StringVar(&cloneSize, "size", "", "Clone size (e.g., 20Gi, default: same as source)")

	cmd.MarkFlagRequired("snapshot")

	return cmd
}

func runDatabaseVerification(ctx context.Context, dbType, dbHost string, dbPort int, dbUser, dbPassword, dbName string, app *App) error {
	switch strings.ToLower(dbType) {
	case "mysql":
		cfg := &database.MySQLConfig{
			Host:     dbHost,
			Port:     dbPort,
			User:     dbUser,
			Password: dbPassword,
			Database: dbName,
			Timeout:  60 * time.Second,
		}

		client, err := database.NewMySQLClient(cfg, app.Logger)
		if err != nil {
			return fmt.Errorf("failed to connect for verification: %w", err)
		}
		defer client.Close()

		return client.Verify(ctx)

	case "postgresql", "postgres":
		cfg := &database.PostgreSQLConfig{
			Host:     dbHost,
			Port:     dbPort,
			User:     dbUser,
			Password: dbPassword,
			Database: dbName,
			SSLMode:  "prefer",
			Timeout:  60 * time.Second,
		}

		client, err := database.NewPostgreSQLClient(cfg, app.Logger)
		if err != nil {
			return fmt.Errorf("failed to connect for verification: %w", err)
		}
		defer client.Close()

		return client.Verify(ctx)

	default:
		return fmt.Errorf("unsupported database type for verification: %s", dbType)
	}
}
