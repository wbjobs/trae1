package cli

import (
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"

	"github.com/rbd-snap/ceph-rbd-snap/internal/ceph"
	"github.com/rbd-snap/ceph-rbd-snap/internal/scheduler"
)

type App struct {
	RootCmd       *cobra.Command
	Logger        *zap.Logger
	RBDClient     *ceph.RBDClient
	Scheduler     *scheduler.Scheduler
	Viper         *viper.Viper
}

func NewApp() *App {
	app := &App{
		Viper: viper.New(),
	}

	app.initLogger()
	app.initRootCmd()
	app.initConfig()

	return app
}

func (a *App) initLogger() {
	config := zap.NewProductionConfig()
	config.EncoderConfig.EncodeTime = zapcore.ISO8601TimeEncoder
	config.EncoderConfig.EncodeLevel = zapcore.CapitalLevelEncoder

	logLevel := os.Getenv("RBD_SNAP_LOG_LEVEL")
	if logLevel == "" {
		logLevel = "info"
	}
	config.Level = zap.NewAtomicLevelAt(parseLogLevel(logLevel))

	var err error
	a.Logger, err = config.Build()
	if err != nil {
		panic(fmt.Sprintf("failed to initialize logger: %v", err))
	}
}

func parseLogLevel(level string) zapcore.Level {
	switch level {
	case "debug":
		return zapcore.DebugLevel
	case "info":
		return zapcore.InfoLevel
	case "warn":
		return zapcore.WarnLevel
	case "error":
		return zapcore.ErrorLevel
	default:
		return zapcore.InfoLevel
	}
}

func (a *App) initRootCmd() {
	a.RootCmd = &cobra.Command{
		Use:   "rbd-snap",
		Short: "Ceph RBD Snapshot Management CLI",
		Long: `A CLI tool for managing Ceph RBD snapshots in Kubernetes environments.
Supports MySQL and PostgreSQL databases with automatic freeze/thaw,
consistency groups for distributed databases, incremental snapshots,
and scheduled snapshot operations.`,
		SilenceUsage:  true,
		SilenceErrors: true,
	}

	a.RootCmd.PersistentFlags().String("config", "", "config file (default is $HOME/.rbd-snap/config.yaml)")
	a.RootCmd.PersistentFlags().String("ceph-cluster", "", "Ceph cluster name")
	a.RootCmd.PersistentFlags().String("ceph-user", "admin", "Ceph user name")
	a.RootCmd.PersistentFlags().String("ceph-keyring", "", "Ceph keyring path")
	a.RootCmd.PersistentFlags().StringSlice("ceph-monitors", []string{}, "Ceph monitor addresses")
	a.RootCmd.PersistentFlags().String("ceph-pool", "rbd", "Ceph pool name")
	a.RootCmd.PersistentFlags().String("kubeconfig", "", "Path to kubeconfig file")
	a.RootCmd.PersistentFlags().String("namespace", "default", "Kubernetes namespace")

	a.RootCmd.AddCommand(NewCreateCmd(a))
	a.RootCmd.AddCommand(NewListCmd(a))
	a.RootCmd.AddCommand(NewRestoreCmd(a))
	a.RootCmd.AddCommand(NewDeleteCmd(a))
	a.RootCmd.AddCommand(NewScheduleCmd(a))
}

func (a *App) initConfig() {
	home, err := os.UserHomeDir()
	if err != nil {
		a.Logger.Warn("Failed to get home directory", zap.Error(err))
		return
	}

	configPath := a.RootCmd.PersistentFlags().Lookup("config").Value.String()
	if configPath == "" {
		configPath = filepath.Join(home, ".rbd-snap", "config.yaml")
	}

	a.Viper.SetConfigFile(configPath)
	a.Viper.SetConfigType("yaml")

	a.Viper.SetEnvPrefix("RBD_SNAP")
	a.Viper.AutomaticEnv()

	if err := a.Viper.ReadInConfig(); err != nil {
		if _, ok := err.(viper.ConfigFileNotFoundError); !ok {
			a.Logger.Warn("Error reading config file", zap.Error(err))
		}
	}

	a.Viper.BindPFlag("ceph.cluster", a.RootCmd.PersistentFlags().Lookup("ceph-cluster"))
	a.Viper.BindPFlag("ceph.user", a.RootCmd.PersistentFlags().Lookup("ceph-user"))
	a.Viper.BindPFlag("ceph.keyring", a.RootCmd.PersistentFlags().Lookup("ceph-keyring"))
	a.Viper.BindPFlag("ceph.monitors", a.RootCmd.PersistentFlags().Lookup("ceph-monitors"))
	a.Viper.BindPFlag("ceph.pool", a.RootCmd.PersistentFlags().Lookup("ceph-pool"))
	a.Viper.BindPFlag("kubeconfig", a.RootCmd.PersistentFlags().Lookup("kubeconfig"))
	a.Viper.BindPFlag("namespace", a.RootCmd.PersistentFlags().Lookup("namespace"))
}

func (a *App) InitRBDClient() error {
	cfg := &ceph.Config{
		ClusterName: a.Viper.GetString("ceph.cluster"),
		UserName:    a.Viper.GetString("ceph.user"),
		Keyring:     a.Viper.GetString("ceph.keyring"),
		Monitors:    a.Viper.GetStringSlice("ceph.monitors"),
		Pool:        a.Viper.GetString("ceph.pool"),
	}

	client, err := ceph.NewRBDClient(cfg, a.Logger)
	if err != nil {
		return fmt.Errorf("failed to initialize RBD client: %w", err)
	}

	a.RBDClient = client
	return nil
}

func (a *App) Execute() error {
	defer a.Logger.Sync()

	if err := a.RootCmd.Execute(); err != nil {
		a.Logger.Error("Command failed", zap.Error(err))
		return err
	}
	return nil
}

func generateSnapshotName(prefix string) string {
	timestamp := time.Now().Format("20060102150405")
	if prefix != "" {
		return fmt.Sprintf("%s-%s", prefix, timestamp)
	}
	return fmt.Sprintf("snap-%s", timestamp)
}

func generateRestorePVCName(pvcName, snapshotName string) string {
	return fmt.Sprintf("%s-restore-%s", pvcName, snapshotName[:8])
}
