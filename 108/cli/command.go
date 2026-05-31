package cli

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net/http"
	"os"
	"time"

	"github.com/spf13/cobra"
	"github.com/wasi-service/runtime/config"
	"github.com/wasi-service/runtime/compile"
	"github.com/wasi-service/runtime/log"
	"github.com/wasi-service/runtime/runtime"
	"go.uber.org/zap"
	"gopkg.in/yaml.v3"
)

var (
	configFile       string
	wasmFile         string
	serviceName      string
	port             int
	instances        int
	envVars          []string
	memoryLimitMB    int
	maxInstructions  int64
	timeoutSeconds   int
	optLevel         string
	moduleVersion    string
	cacheDir         string
)

func NewRootCommand() *cobra.Command {
	return &cobra.Command{
		Use:   "wasm-service",
		Short: "WASI microservice runtime",
		Long:  "A microservice runtime that runs WebAssembly modules as service units",
	}
}

func NewRunCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "run",
		Short: "Run the WASI runtime",
		RunE:  runRun,
	}

	cmd.Flags().StringVarP(&configFile, "config", "c", "config.yaml", "Configuration file path")

	return cmd
}

func NewDeployCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "deploy",
		Short: "Deploy a WASM module to the runtime",
		Long:  `Deploy a .wasm file to the runtime. This command reads a WASM module,
validates it, and registers it with the runtime for execution.`,
		Example: `  wasm-service deploy --wasm hello.wasm --name hello-service --port 8080 --memory-limit 64 --max-instructions 10000000 --timeout 10`,
		RunE:    runDeploy,
	}

	cmd.Flags().StringVarP(&wasmFile, "wasm", "w", "", "Path to the WASM module file (required)")
	cmd.Flags().StringVarP(&serviceName, "name", "n", "", "Service name (required)")
	cmd.Flags().IntVarP(&port, "port", "p", 8080, "Service port")
	cmd.Flags().IntVarP(&instances, "instances", "i", 1, "Number of instances")
	cmd.Flags().StringArrayVarP(&envVars, "env", "e", []string{}, "Environment variables (KEY=VALUE)")
	cmd.Flags().IntVarP(&memoryLimitMB, "memory-limit", "m", 64, "Memory limit in MB")
	cmd.Flags().Int64VarP(&maxInstructions, "max-instructions", "x", 10_000_000, "Maximum number of WebAssembly instructions per request")
	cmd.Flags().IntVarP(&timeoutSeconds, "timeout", "t", 10, "Request timeout in seconds")

	cmd.MarkFlagRequired("wasm")
	cmd.MarkFlagRequired("name")

	return cmd
}

func NewListCommand() *cobra.Command {
	return &cobra.Command{
		Use:   "list",
		Short: "List deployed services",
		RunE:  runList,
	}
}

func NewStopCommand() *cobra.Command {
	return &cobra.Command{
		Use:   "stop [service-name]",
		Short: "Stop a running service",
		Args:  cobra.ExactArgs(1),
		RunE:  runStop,
	}
}

func NewLogsCommand() *cobra.Command {
	return &cobra.Command{
		Use:   "logs [service-name]",
		Short: "Get logs from a service",
		Args:  cobra.ExactArgs(1),
		RunE:  runLogs,
	}
}

func NewCompileCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "compile",
		Short: "Pre-compile WASM modules to native code (AoT)",
		Long: `Pre-compile WASM modules to native machine code using AoT compilation.
This reduces startup time from ~200ms to ~5ms.
Cached compilation artifacts are stored and reused on subsequent loads.`,
		Example: `  wasm-service compile --wasm hello.wasm --version v1.0.0 --opt-level O2
  wasm-service compile --wasm service.wasm --opt-level O3 --cache-dir ./aot-cache
  wasm-service compile --wasm module.wasm --version v2 --output ./compiled`,
		RunE:  runCompile,
	}

	cmd.Flags().StringVarP(&wasmFile, "wasm", "w", "", "Path to the WASM module file (required)")
	cmd.Flags().StringVarP(&moduleVersion, "version", "v", "latest", "Module version for cache management")
	cmd.Flags().StringVarP(&optLevel, "opt-level", "O", "O2", "Optimization level (O0, O1, O2, O3)")
	cmd.Flags().StringVarP(&cacheDir, "cache-dir", "d", "./aot-cache", "Cache directory for compiled artifacts")

	cmd.MarkFlagRequired("wasm")

	return cmd
}

func NewCacheCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "cache",
		Short: "Manage AoT compilation cache",
		Long:  `Manage the AoT compilation cache. View stats, clear cache, or list entries.`,
	}

	cmd.AddCommand(&cobra.Command{
		Use:   "stats",
		Short: "Show cache statistics",
		RunE:  runCacheStats,
	})

	cmd.AddCommand(&cobra.Command{
		Use:   "clear",
		Short: "Clear all cached entries",
		RunE:  runCacheClear,
	})

	cmd.AddCommand(&cobra.Command{
		Use:   "list",
		Short: "List cached modules",
		RunE:  runCacheList,
	})

	return cmd
}

func runRun(cmd *cobra.Command, args []string) error {
	logger, err := log.InitLogger("info")
	if err != nil {
		return fmt.Errorf("failed to init logger: %w", err)
	}
	defer logger.Sync()

	cfgManager, err := config.NewConfigManager(configFile)
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	cfg := cfgManager.Get()
	if err := cfg.Validate(); err != nil {
		return fmt.Errorf("invalid config: %w", err)
	}

	rt, err := runtime.NewRuntime(cfgManager, cfg.ConsulAddress, cfg.InstanceLimit, logger)
	if err != nil {
		return fmt.Errorf("failed to create runtime: %w", err)
	}

	ctx := context.Background()

	for _, svcCfg := range cfg.Services {
		if err := rt.StartService(ctx, &svcCfg); err != nil {
			logger.Error("failed to start service", zap.String("service", svcCfg.Name), zap.Error(err))
		}
	}

	logger.Info("WASI runtime started", zap.String("listen_addr", cfg.ListenAddr))

	select {}
}

func runDeploy(cmd *cobra.Command, args []string) error {
	if wasmFile == "" {
		return fmt.Errorf("wasm file is required")
	}

	if serviceName == "" {
		return fmt.Errorf("service name is required")
	}

	wasmBytes, err := os.ReadFile(wasmFile)
	if err != nil {
		return fmt.Errorf("failed to read wasm file: %w", err)
	}

	if len(wasmBytes) < 4 || string(wasmBytes[:4]) != "\0asm" {
		return fmt.Errorf("invalid wasm file: missing magic header")
	}

	svcCfg := &config.ServiceConfig{
		Name:            serviceName,
		Module:          wasmFile,
		Port:            port,
		Instances:       instances,
		EnvVars:         make(map[string]string),
		MemoryLimitMB:   memoryLimitMB,
		MaxInstructions: maxInstructions,
		TimeoutSeconds:  timeoutSeconds,
	}

	for _, e := range envVars {
		for i := 0; i < len(e); i++ {
			if e[i] == '=' {
				svcCfg.EnvVars[e[:i]] = e[i+1:]
				break
			}
		}
	}

	cfgPath := "services.yaml"
	var existingCfg *config.RuntimeConfig

	if data, err := os.ReadFile(cfgPath); err == nil {
		existingCfg = &config.RuntimeConfig{}
		yaml.Unmarshal(data, existingCfg)
	} else {
		existingCfg = &config.RuntimeConfig{
			LogLevel:               "info",
			ListenAddr:             ":8080",
			InstanceLimit:          100,
			DefaultMemoryLimitMB:   64,
			DefaultMaxInstructions: 10_000_000,
			DefaultTimeoutSeconds:  10,
		}
	}

	existingCfg.Services = append(existingCfg.Services, *svcCfg)

	var buf bytes.Buffer
	encoder := yaml.NewEncoder(&buf)
	encoder.SetIndent(2)
	encoder.Encode(existingCfg)

	if err := os.WriteFile(cfgPath, buf.Bytes(), 0644); err != nil {
		return fmt.Errorf("failed to write config: %w", err)
	}

	fmt.Printf("Service '%s' deployed successfully!\n", serviceName)
	fmt.Printf("Config written to %s\n", cfgPath)
	fmt.Printf("Run 'wasm-service run --config %s' to start the service\n", cfgPath)

	return nil
}

func runList(cmd *cobra.Command, args []string) error {
	resp, err := http.Get("http://localhost:8080/v1/services")
	if err != nil {
		fmt.Println("Runtime not running or not accessible")
		return nil
	}
	defer resp.Body.Close()

	body, _ := io.ReadAll(resp.Body)
	fmt.Println(string(body))
	return nil
}

func runStop(cmd *cobra.Command, args []string) error {
	serviceName := args[0]

	resp, err := http.Post(fmt.Sprintf("http://localhost:8080/v1/services/%s/stop", serviceName), "application/json", nil)
	if err != nil {
		return fmt.Errorf("failed to stop service: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("failed to stop service: status %d", resp.StatusCode)
	}

	fmt.Printf("Service '%s' stopped\n", serviceName)
	return nil
}

func runLogs(cmd *cobra.Command, args []string) error {
	serviceName := args[0]

	resp, err := http.Get(fmt.Sprintf("http://localhost:8080/v1/services/%s/logs", serviceName))
	if err != nil {
		return fmt.Errorf("failed to get logs: %w", err)
	}
	defer resp.Body.Close()

	body, _ := io.ReadAll(resp.Body)
	fmt.Print(string(body))
	return nil
}

func runCompile(cmd *cobra.Command, args []string) error {
	logger, err := log.InitLogger("info")
	if err != nil {
		return fmt.Errorf("failed to init logger: %w", err)
	}
	defer logger.Sync()

	if wasmFile == "" {
		return fmt.Errorf("wasm file is required")
	}

	var opt compile.OptimizationLevel
	switch optLevel {
	case "O0":
		opt = compile.OptLevel_O0
	case "O1":
		opt = compile.OptLevel_O1
	case "O2":
		opt = compile.OptLevel_O2
	case "O3":
		opt = compile.OptLevel_O3
	default:
		opt = compile.OptLevel_O2
	}

	cacheManager, err := compile.NewAOTCacheManager(cacheDir, opt, logger)
	if err != nil {
		return fmt.Errorf("failed to create cache manager: %w", err)
	}

	fmt.Printf("Compiling %s with optimization level %s...\n", wasmFile, optLevel)
	fmt.Printf("Cache directory: %s\n", cacheDir)

	startTime := time.Now()

	if err := cacheManager.CompileWasm(wasmFile, moduleVersion); err != nil {
		return fmt.Errorf("compilation failed: %w", err)
	}

	elapsed := time.Since(startTime)

	stats, _, _ := cacheManager.GetStats()
	fmt.Printf("\nCompilation successful!\n")
	fmt.Printf("Time: %v\n", elapsed)
	fmt.Printf("Cache entries: %d\n", stats)
	fmt.Printf("Cached at: %s/%s.cache\n", cacheDir, wasmFile)

	return nil
}

func runCacheStats(cmd *cobra.Command, args []string) error {
	logger, err := log.InitLogger("info")
	if err != nil {
		return fmt.Errorf("failed to init logger: %w", err)
	}

	cacheManager, err := compile.NewAOTCacheManager(cacheDir, compile.OptLevel_O2, logger)
	if err != nil {
		return fmt.Errorf("failed to create cache manager: %w", err)
	}

	entries, size, hits := cacheManager.GetStats()

	fmt.Printf("AoT Cache Statistics\n")
	fmt.Printf("==================\n")
	fmt.Printf("Entries: %d\n", entries)
	fmt.Printf("Size: %s\n", formatBytes(size))
	fmt.Printf("Max Size: %s (%.1f%% used)\n", formatBytes(compile.MaxCacheSizeBytes), float64(size)/float64(compile.MaxCacheSizeBytes)*100)
	fmt.Printf("Total Hits: %d\n", hits)
	fmt.Printf("Cache Dir: %s\n", cacheDir)

	return nil
}

func runCacheClear(cmd *cobra.Command, args []string) error {
	logger, err := log.InitLogger("info")
	if err != nil {
		return fmt.Errorf("failed to init logger: %w", err)
	}

	cacheManager, err := compile.NewAOTCacheManager(cacheDir, compile.OptLevel_O2, logger)
	if err != nil {
		return fmt.Errorf("failed to create cache manager: %w", err)
	}

	if err := cacheManager.Clear(); err != nil {
		return fmt.Errorf("failed to clear cache: %w", err)
	}

	fmt.Println("Cache cleared successfully")
	return nil
}

func runCacheList(cmd *cobra.Command, args []string) error {
	logger, err := log.InitLogger("info")
	if err != nil {
		return fmt.Errorf("failed to init logger: %w", err)
	}

	cacheManager, err := compile.NewAOTCacheManager(cacheDir, compile.OptLevel_O2, logger)
	if err != nil {
		return fmt.Errorf("failed to create cache manager: %w", err)
	}

	entries, _, _ := cacheManager.GetStats()
	fmt.Printf("Cached modules: %d\n\n", entries)

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
	return fmt.Sprintf("%.1f %cB", float64(bytes)/float64(div), "KMGTPE"[exp])
}

func Execute() {
	root := NewRootCommand()
	root.AddCommand(NewRunCommand())
	root.AddCommand(NewDeployCommand())
	root.AddCommand(NewListCommand())
	root.AddCommand(NewStopCommand())
	root.AddCommand(NewLogsCommand())
	root.AddCommand(NewCompileCommand())
	root.AddCommand(NewCacheCommand())

	if err := root.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
