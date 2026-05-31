package cli

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"text/tabwriter"
	"time"

	"github.com/google/uuid"
	"github.com/spf13/cobra"
	"go.uber.org/zap"

	"github.com/rbd-snap/ceph-rbd-snap/internal/ceph"
	"github.com/rbd-snap/ceph-rbd-snap/internal/database"
	"github.com/rbd-snap/ceph-rbd-snap/internal/k8s"
	"github.com/rbd-snap/ceph-rbd-snap/internal/scheduler"
)

func NewScheduleCmd(app *App) *cobra.Command {
	cmd := &cobra.Command{
		Use:   "schedule",
		Short: "Manage scheduled RBD snapshots",
		Long: `Create, list, remove, pause, resume scheduled RBD snapshot tasks.
Uses cron expressions for scheduling. Supports one-time immediate execution.`,
	}

	cmd.AddCommand(NewScheduleAddCmd(app))
	cmd.AddCommand(NewScheduleListCmd(app))
	cmd.AddCommand(NewScheduleRemoveCmd(app))
	cmd.AddCommand(NewSchedulePauseCmd(app))
	cmd.AddCommand(NewScheduleResumeCmd(app))
	cmd.AddCommand(NewScheduleRunCmd(app))
	cmd.AddCommand(NewScheduleDaemonCmd(app))

	return cmd
}

type ScheduledTaskExecutor struct {
	app *App
}

func (e *ScheduledTaskExecutor) ExecuteSnapshot(ctx context.Context, task *scheduler.SnapshotTask) error {
	e.app.Logger.Info("Executing scheduled snapshot task",
		zap.String("task_id", task.ID),
		zap.String("task_name", task.Name),
		zap.Strings("images", task.Images),
		zap.Bool("quick_freeze", task.QuickFreeze),
	)

	if err := e.app.InitRBDClient(); err != nil {
		return err
	}
	defer e.app.RBDClient.Close()

	snapName := generateSnapshotName(fmt.Sprintf("scheduled-%s", task.Name))
	if task.Incremental {
		snapName += "-incr"
	}

	startTime := time.Now()

	if task.QuickFreeze && task.Database != nil {
		return e.executeQuickFreezeSnapshot(ctx, task, snapName, startTime)
	}

	return e.executeFullFreezeSnapshot(ctx, task, snapName, startTime)
}

func (e *ScheduledTaskExecutor) executeQuickFreezeSnapshot(
	ctx context.Context,
	task *scheduler.SnapshotTask,
	snapName string,
	startTime time.Time,
) error {
	e.app.Logger.Info("Quick freeze mode: Step 1 - Create RBD snapshots first")

	createdSnapshots := make(map[string]string)
	for _, image := range task.Images {
		err := e.app.RBDClient.CreateSnapshot(image, snapName)
		if err != nil {
			for img, snap := range createdSnapshots {
				_ = e.app.RBDClient.DeleteSnapshot(img, snap)
			}
			return fmt.Errorf("RBD snapshot failed for image %s: %w", image, err)
		}
		createdSnapshots[image] = snapName
		_ = e.app.RBDClient.ProtectSnapshot(image, snapName)
	}

	snapshotDuration := time.Since(startTime)
	e.app.Logger.Info("RBD snapshots created",
		zap.Duration("duration", snapshotDuration),
		zap.Int("count", len(createdSnapshots)),
	)

	if task.Database != nil {
		e.app.Logger.Info("Quick freeze mode: Step 2 - Brief database freeze (flush logs only)")

		freezeStart := time.Now()

		switch strings.ToLower(task.Database.Type) {
		case "mysql":
			cfg := &database.MySQLConfig{
				Host:     task.Database.Host,
				Port:     task.Database.Port,
				User:     task.Database.User,
				Password: task.Database.Password,
				Database: task.Database.Database,
				Timeout:  5 * time.Second,
			}
			mysqlClient, err := database.NewMySQLClient(cfg, e.app.Logger)
			if err == nil {
				_ = mysqlClient.FreezeQuick(ctx)
				mysqlClient.Close()
			}

		case "postgresql", "postgres":
			cfg := &database.PostgreSQLConfig{
				Host:     task.Database.Host,
				Port:     task.Database.Port,
				User:     task.Database.User,
				Password: task.Database.Password,
				Database: task.Database.Database,
				SSLMode:  "prefer",
				Timeout:  5 * time.Second,
			}
			pgClient, err := database.NewPostgreSQLClient(cfg, e.app.Logger)
			if err == nil {
				_, _ = pgClient.FreezeQuick(ctx)
				pgClient.Close()
			}
		}

		freezeDuration := time.Since(freezeStart)
		e.app.Logger.Info("Quick freeze completed",
			zap.Duration("freeze_window", freezeDuration),
		)
	}

	e.app.Logger.Info("Quick freeze snapshot completed",
		zap.Duration("total_duration", time.Since(startTime)),
	)
	return nil
}

func (e *ScheduledTaskExecutor) executeFullFreezeSnapshot(
	ctx context.Context,
	task *scheduler.SnapshotTask,
	snapName string,
	startTime time.Time,
) error {
	var unfreezeFunc func()

	if task.Database != nil {
		e.app.Logger.Info("Full freeze mode: Step 1 - Lock database before snapshot")

		switch strings.ToLower(task.Database.Type) {
		case "mysql":
			cfg := &database.MySQLConfig{
				Host:     task.Database.Host,
				Port:     task.Database.Port,
				User:     task.Database.User,
				Password: task.Database.Password,
				Database: task.Database.Database,
				Timeout:  30 * time.Second,
			}
			mysqlClient, err := database.NewMySQLClient(cfg, e.app.Logger)
			if err != nil {
				return fmt.Errorf("failed to connect to MySQL: %w", err)
			}
			defer mysqlClient.Close()

			if err := mysqlClient.Freeze(ctx); err != nil {
				return fmt.Errorf("failed to freeze MySQL: %w", err)
			}
			unfreezeFunc = func() {
				if err := mysqlClient.Unfreeze(context.Background()); err != nil {
					e.app.Logger.Error("Failed to unfreeze MySQL", zap.Error(err))
				}
			}

		case "postgresql", "postgres":
			cfg := &database.PostgreSQLConfig{
				Host:     task.Database.Host,
				Port:     task.Database.Port,
				User:     task.Database.User,
				Password: task.Database.Password,
				Database: task.Database.Database,
				SSLMode:  "prefer",
				Timeout:  30 * time.Second,
			}
			pgClient, err := database.NewPostgreSQLClient(cfg, e.app.Logger)
			if err != nil {
				return fmt.Errorf("failed to connect to PostgreSQL: %w", err)
			}
			defer pgClient.Close()

			if _, err := pgClient.Freeze(ctx); err != nil {
				return fmt.Errorf("failed to freeze PostgreSQL: %w", err)
			}
			unfreezeFunc = func() {
				if err := pgClient.Unfreeze(context.Background()); err != nil {
					e.app.Logger.Error("Failed to unfreeze PostgreSQL", zap.Error(err))
				}
			}
		}

		if unfreezeFunc != nil {
			defer unfreezeFunc()
		}
	}

	e.app.Logger.Info("Full freeze mode: Step 2 - Create RBD snapshots while locked")

	if len(task.Images) > 1 {
		group := &ceph.ConsistencyGroup{
			Name:   task.GroupID,
			Images: task.Images,
		}
		err := e.app.RBDClient.CreateConsistencySnapshot(group, snapName)
		if err != nil {
			return fmt.Errorf("consistency snapshot failed: %w", err)
		}
	} else {
		for _, image := range task.Images {
			err := e.app.RBDClient.CreateSnapshot(image, snapName)
			if err != nil {
				return fmt.Errorf("failed to snapshot image %s: %w", image, err)
			}
			_ = e.app.RBDClient.ProtectSnapshot(image, snapName)
		}
	}

	e.app.Logger.Info("Full freeze snapshot completed",
		zap.Duration("total_duration", time.Since(startTime)),
	)
	return nil
}

func NewScheduleAddCmd(app *App) *cobra.Command {
	var (
		name        string
		cronExpr    string
		pvcNames    []string
		rbdImages   []string
		dbType      string
		dbHost      string
		dbPort      int
		dbUser      string
		dbPassword  string
		dbName      string
		incremental bool
		quickFreeze bool
	)

	cmd := &cobra.Command{
		Use:   "add",
		Short: "Add a new scheduled snapshot task",
		Long: `Add a new scheduled snapshot task with cron expression.
Supports both quick freeze (default, recommended for large databases)
and full freeze modes.`,
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx := context.Background()
			k8sClient, err := k8s.NewK8sClient(&k8s.K8sConfig{
				Kubeconfig: app.Viper.GetString("kubeconfig"),
				Namespace:  app.Viper.GetString("namespace"),
			}, app.Logger)
			if err != nil {
				return err
			}

			targetImages, err := resolveRBDImages(ctx, k8sClient, pvcNames, rbdImages, app.Viper.GetString("namespace"))
			if err != nil {
				return err
			}

			if len(targetImages) == 0 {
				return fmt.Errorf("no RBD images specified")
			}

			taskID := uuid.New().String()[:8]
			groupID := fmt.Sprintf("cg-%s", taskID)

			task := &scheduler.SnapshotTask{
				ID:          taskID,
				Name:        name,
				CronExpr:    cronExpr,
				Images:      targetImages,
				Incremental: incremental,
				GroupID:     groupID,
				Enabled:     true,
				CreatedAt:   time.Now(),
				QuickFreeze: quickFreeze,
			}

			if dbType != "" {
				task.Database = &scheduler.DatabaseConfig{
					Type:     dbType,
					Host:     dbHost,
					Port:     dbPort,
					User:     dbUser,
					Password: dbPassword,
					Database: dbName,
				}
			}

			executor := &ScheduledTaskExecutor{app: app}
			sched := scheduler.NewScheduler(app.Logger, executor)

			if err := sched.AddTask(task); err != nil {
				return fmt.Errorf("failed to add scheduled task: %w", err)
			}

			fmt.Printf("Scheduled task added:\n")
			fmt.Printf("  ID:           %s\n", task.ID)
			fmt.Printf("  Name:         %s\n", task.Name)
			fmt.Printf("  Cron:         %s\n", task.CronExpr)
			fmt.Printf("  Images:       %v\n", task.Images)
			fmt.Printf("  Quick Freeze: %v (recommended for large DBs)\n", task.QuickFreeze)
			fmt.Printf("  Incremental:  %v\n", task.Incremental)
			fmt.Printf("  DB Type:      %s\n", dbType)
			if task.NextRun != nil {
				fmt.Printf("  Next run:     %s\n", task.NextRun.Format(time.RFC3339))
			}

			return nil
		},
	}

	cmd.Flags().StringVar(&name, "name", "", "Task name (required)")
	cmd.Flags().StringVar(&cronExpr, "cron", "", "Cron expression (e.g., '0 2 * * *' for daily)")
	cmd.Flags().StringSliceVar(&pvcNames, "pvc", []string{}, "Kubernetes PVC names")
	cmd.Flags().StringSliceVar(&rbdImages, "rbd-image", []string{}, "RBD image names")
	cmd.Flags().StringVar(&dbType, "db-type", "", "Database type: mysql or postgresql")
	cmd.Flags().StringVar(&dbHost, "db-host", "localhost", "Database host")
	cmd.Flags().IntVar(&dbPort, "db-port", 3306, "Database port")
	cmd.Flags().StringVar(&dbUser, "db-user", "root", "Database user")
	cmd.Flags().StringVar(&dbPassword, "db-password", "", "Database password")
	cmd.Flags().StringVar(&dbName, "db-name", "", "Database name")
	cmd.Flags().BoolVar(&incremental, "incremental", false, "Create incremental snapshots")
	cmd.Flags().BoolVar(&quickFreeze, "quick-freeze", true, "Use quick freeze mode (RBD snapshot first, then brief log flush)")

	cmd.MarkFlagRequired("name")
	cmd.MarkFlagRequired("cron")

	return cmd
}

func NewScheduleListCmd(app *App) *cobra.Command {
	cmd := &cobra.Command{
		Use:   "list",
		Short: "List all scheduled snapshot tasks",
		RunE: func(cmd *cobra.Command, args []string) error {
			executor := &ScheduledTaskExecutor{app: app}
			sched := scheduler.NewScheduler(app.Logger, executor)

			tasks := sched.ListTasks()

			if len(tasks) == 0 {
				fmt.Println("No scheduled tasks found")
				return nil
			}

			w := tabwriter.NewWriter(os.Stdout, 0, 0, 3, ' ', 0)
			fmt.Fprintln(w, "ID\tNAME\tCRON\tIMAGES\tENABLED\tNEXT RUN\tLAST RUN")

			for _, task := range tasks {
				lastRun := "-"
				if task.LastRun != nil {
					lastRun = task.LastRun.Format(time.RFC3339)
				}
				nextRun := "-"
				if task.NextRun != nil {
					nextRun = task.NextRun.Format(time.RFC3339)
				}
				enabled := "Yes"
				if !task.Enabled {
					enabled = "No"
				}
				fmt.Fprintf(w, "%s\t%s\t%s\t%d\t%s\t%s\t%s\n",
					task.ID,
					task.Name,
					task.CronExpr,
					len(task.Images),
					enabled,
					nextRun,
					lastRun,
				)
			}
			w.Flush()

			return nil
		},
	}

	return cmd
}

func NewScheduleRemoveCmd(app *App) *cobra.Command {
	var taskID string

	cmd := &cobra.Command{
		Use:   "remove",
		Short: "Remove a scheduled snapshot task",
		RunE: func(cmd *cobra.Command, args []string) error {
			executor := &ScheduledTaskExecutor{app: app}
			sched := scheduler.NewScheduler(app.Logger, executor)

			if err := sched.RemoveTask(taskID); err != nil {
				return fmt.Errorf("failed to remove task: %w", err)
			}

			fmt.Printf("Task %s removed successfully\n", taskID)
			return nil
		},
	}

	cmd.Flags().StringVar(&taskID, "id", "", "Task ID")
	cmd.MarkFlagRequired("id")

	return cmd
}

func NewSchedulePauseCmd(app *App) *cobra.Command {
	var taskID string

	cmd := &cobra.Command{
		Use:   "pause",
		Short: "Pause a scheduled snapshot task",
		RunE: func(cmd *cobra.Command, args []string) error {
			executor := &ScheduledTaskExecutor{app: app}
			sched := scheduler.NewScheduler(app.Logger, executor)

			if err := sched.PauseTask(taskID); err != nil {
				return fmt.Errorf("failed to pause task: %w", err)
			}

			fmt.Printf("Task %s paused successfully\n", taskID)
			return nil
		},
	}

	cmd.Flags().StringVar(&taskID, "id", "", "Task ID")
	cmd.MarkFlagRequired("id")

	return cmd
}

func NewScheduleResumeCmd(app *App) *cobra.Command {
	var taskID string

	cmd := &cobra.Command{
		Use:   "resume",
		Short: "Resume a paused scheduled snapshot task",
		RunE: func(cmd *cobra.Command, args []string) error {
			executor := &ScheduledTaskExecutor{app: app}
			sched := scheduler.NewScheduler(app.Logger, executor)

			if err := sched.ResumeTask(taskID); err != nil {
				return fmt.Errorf("failed to resume task: %w", err)
			}

			fmt.Printf("Task %s resumed successfully\n", taskID)
			return nil
		},
	}

	cmd.Flags().StringVar(&taskID, "id", "", "Task ID")
	cmd.MarkFlagRequired("id")

	return cmd
}

func NewScheduleRunCmd(app *App) *cobra.Command {
	var taskID string

	cmd := &cobra.Command{
		Use:   "run",
		Short: "Run a scheduled snapshot task immediately",
		RunE: func(cmd *cobra.Command, args []string) error {
			executor := &ScheduledTaskExecutor{app: app}
			sched := scheduler.NewScheduler(app.Logger, executor)

			if err := sched.RunTaskNow(taskID); err != nil {
				return fmt.Errorf("failed to run task: %w", err)
			}

			fmt.Printf("Task %s triggered for immediate execution\n", taskID)
			return nil
		},
	}

	cmd.Flags().StringVar(&taskID, "id", "", "Task ID")
	cmd.MarkFlagRequired("id")

	return cmd
}

func NewScheduleDaemonCmd(app *App) *cobra.Command {
	cmd := &cobra.Command{
		Use:   "daemon",
		Short: "Run the scheduler as a daemon process",
		Long: `Run the scheduler daemon that executes scheduled snapshot tasks.
This process will run in the foreground until interrupted.`,
		RunE: func(cmd *cobra.Command, args []string) error {
			executor := &ScheduledTaskExecutor{app: app}
			sched := scheduler.NewScheduler(app.Logger, executor)

			sched.Start()
			defer sched.Stop()

			app.Logger.Info("Scheduler daemon started")
			fmt.Println("Scheduler daemon running. Press Ctrl+C to stop...")

			sigCh := make(chan os.Signal, 1)
			signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
			<-sigCh

			app.Logger.Info("Received shutdown signal, stopping scheduler")
			fmt.Println("\nShutting down scheduler...")

			return nil
		},
	}

	return cmd
}
