package main

import (
	"fmt"
	"io"
	"net/http"
	"os"
	"os/signal"
	"syscall"

	"github.com/gin-gonic/gin"
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"

	"transcode-gateway/internal/api"
	"transcode-gateway/internal/cgroup"
	"transcode-gateway/internal/config"
	"transcode-gateway/internal/logger"
	"transcode-gateway/internal/probe"
	"transcode-gateway/internal/scheduler"
	"transcode-gateway/internal/transcoder"
)

func main() {
	var configPath string

	rootCmd := &cobra.Command{
		Use:   "transcode-gateway",
		Short: "流媒体转码网关",
	}

	serveCmd := &cobra.Command{
		Use:   "serve",
		Short: "启动网关服务",
		RunE: func(cmd *cobra.Command, args []string) error {
			return serve(configPath)
		},
	}

	listCmd := &cobra.Command{
		Use:   "list",
		Short: "列出所有运行中的转码任务",
		RunE: func(cmd *cobra.Command, args []string) error {
			return listTasks(configPath)
		},
	}

	rootCmd.PersistentFlags().StringVarP(&configPath, "config", "c", "config.yaml", "配置文件路径")

	rootCmd.AddCommand(serveCmd)
	rootCmd.AddCommand(listCmd)

	if err := rootCmd.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func serve(path string) error {
	cfg, err := config.Load(path)
	if err != nil {
		return err
	}

	log := logger.New(cfg)
	defer log.Close()

	trans := transcoder.NewManager(cfg, log)
	mon := probe.New(cfg, log)
	cgm := cgroup.New(cfg, log)
	sched := scheduler.New(cfg, log, trans, mon, cgm)
	defer sched.Close()

	go sched.ZombieCleanupLoop()

	gin.SetMode(gin.ReleaseMode)
	r := gin.Default()

	h := api.NewHandler(sched)
	h.Register(r)

	addr := cfg.Addr()
	log.WithFields(logrus.Fields{"addr": addr}).Info("网关服务已启动")

	go func() {
		if err := r.Run(addr); err != nil && err != http.ErrServerClosed {
			log.Errorf("HTTP 服务异常退出: %v", err)
		}
	}()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh

	log.Info("收到退出信号，正在清理资源...")
	return nil
}

func listTasks(path string) error {
	cfg, err := config.Load(path)
	if err != nil {
		return err
	}

	client := &http.Client{}
	resp, err := client.Get(fmt.Sprintf("http://%s/api/v1/tasks", cfg.Addr()))
	if err != nil {
		return fmt.Errorf("无法访问网关服务: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("网关返回状态: %s", resp.Status)
	}

	buf, err := io.ReadAll(resp.Body)
	if err != nil {
		return err
	}
	fmt.Println(string(buf))
	return nil
}
