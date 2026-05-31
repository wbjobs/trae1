package cli

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/grpctest/grpctest/internal/config"
	"github.com/grpctest/grpctest/internal/diff"
	"github.com/grpctest/grpctest/internal/proxy"
	"github.com/grpctest/grpctest/internal/replay"
	"github.com/grpctest/grpctest/internal/report"
	"github.com/grpctest/grpctest/internal/runner"
	"github.com/grpctest/grpctest/internal/schema"
	"github.com/grpctest/grpctest/internal/session"
	"github.com/spf13/cobra"
)

var (
	cfgFile       string
	outputFile    string
	verbose       bool
	listenAddr    string
	targetAddr    string
	targetInsecure bool
	sessionFile   string
	useReflection bool
	protoFiles    []string
	importPaths   []string
	timeoutStr    string
	ignoreFields  []string
)

func NewRootCmd() *cobra.Command {
	root := &cobra.Command{
		Use:   "grpctest",
		Short: "gRPC contract testing CLI tool",
		Long: `grpctest runs gRPC contract tests from JSON-based configuration.

It supports:
- proto files or gRPC reflection for schema retrieval
- dependency-based step ordering (depends_on)
- field-path assertions (e.g. "user.name" == "张三")
- JUnit XML report output for CI/CD integration
- traffic recording via local proxy (record)
- replay of recorded sessions with diff-based comparison (replay)`,
	}
	root.AddCommand(newRunCmd())
	root.AddCommand(newListCmd())
	root.AddCommand(newRecordCmd())
	root.AddCommand(newReplayCmd())
	return root
}

func newRunCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "run",
		Short: "Run gRPC contract tests",
		RunE: func(cmd *cobra.Command, args []string) error {
			return run()
		},
	}
	cmd.Flags().StringVarP(&cfgFile, "config", "c", "", "path to JSON config file (required)")
	cmd.Flags().StringVarP(&outputFile, "output", "o", "junit.xml", "path to JUnit XML output file")
	cmd.Flags().BoolVarP(&verbose, "verbose", "v", false, "verbose output")
	_ = cmd.MarkFlagRequired("config")
	return cmd
}

func newListCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "list",
		Short: "List available services and methods (via proto or reflection)",
		RunE: func(cmd *cobra.Command, args []string) error {
			return listServices()
		},
	}
	cmd.Flags().StringVarP(&cfgFile, "config", "c", "", "path to JSON config file (required)")
	_ = cmd.MarkFlagRequired("config")
	return cmd
}

func newRecordCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "record",
		Short: "Start a local gRPC proxy and record all request/response pairs to YAML",
		Long: `Listen on a local port, forward gRPC calls to the target service, and record each
request/response to a YAML session file. The recording requires either proto files or
server reflection to decode the wire format.

Example:
  grpctest record --listen :50052 --target localhost:50051 \
      --insecure --use-reflection --output session.yaml`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runRecord()
		},
	}
	cmd.Flags().StringVar(&listenAddr, "listen", ":50052", "local listen address")
	cmd.Flags().StringVar(&targetAddr, "target", "", "target gRPC service address (required)")
	cmd.Flags().BoolVar(&targetInsecure, "insecure", false, "use plaintext connection to target")
	cmd.Flags().BoolVar(&useReflection, "use-reflection", false, "use gRPC reflection to discover schema")
	cmd.Flags().StringArrayVar(&protoFiles, "proto", nil, "proto files (repeatable)")
	cmd.Flags().StringArrayVar(&importPaths, "import-path", nil, "proto import paths (repeatable)")
	cmd.Flags().StringVar(&timeoutStr, "timeout", "10s", "call timeout")
	cmd.Flags().StringVarP(&outputFile, "output", "o", "session.yaml", "output YAML session file")
	_ = cmd.MarkFlagRequired("target")
	return cmd
}

func newReplayCmd() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "replay",
		Short: "Replay a recorded YAML session against the target, compare with diff",
		Long: `Replay each recorded entry in the YAML session file against the target service, and
compare the new response against the recorded baseline. Diffs are shown in a unified
format; fields listed in --ignore (or recorded via entry.ignore) are excluded from
comparison. Prints success rate at the end.

Example:
  grpctest replay --session session.yaml --ignore createdAt --ignore updatedAt`,
		RunE: func(cmd *cobra.Command, args []string) error {
			return runReplay()
		},
	}
	cmd.Flags().StringVar(&sessionFile, "session", "", "path to YAML session file (required)")
	cmd.Flags().StringArrayVar(&ignoreFields, "ignore", nil, "field paths to ignore during comparison (repeatable)")
	cmd.Flags().BoolVarP(&verbose, "verbose", "v", false, "verbose output (show full diffs)")
	_ = cmd.MarkFlagRequired("session")
	return cmd
}

func run() error {
	cfg, err := config.Load(cfgFile)
	if err != nil {
		return err
	}
	ctx, cancel := signalContext()
	defer cancel()

	r := runner.NewRunner(cfg)
	results, err := r.Run(ctx)
	if err != nil {
		return err
	}
	if err := report.WriteJUnitXML(outputFile, results); err != nil {
		return err
	}
	failCount := 0
	for _, s := range results {
		for _, c := range s.Cases {
			status := "PASS"
			if c.Err != nil {
				status = "ERROR"
				failCount++
			} else {
				for _, a := range c.Assertions {
					if !a.Passed {
						status = "FAIL"
					}
				}
				if status == "FAIL" {
					failCount++
				}
			}
			fmt.Printf("[%s] %s (%s) - %v\n", status, c.Name, c.Classname, c.Duration)
			for _, a := range c.Assertions {
				fmt.Printf("  %s\n", a.Detail)
			}
			if c.Err != nil {
				fmt.Printf("  error: %v\n", c.Err)
			}
			if verbose {
				fmt.Printf("  response: %s\n", c.Details)
			}
		}
	}
	fmt.Printf("\nReport written to %s (failures=%d)\n", outputFile, failCount)
	if failCount > 0 {
		os.Exit(1)
	}
	return nil
}

func listServices() error {
	return fmt.Errorf("list command not yet implemented")
}

func runRecord() error {
	timeout, err := time.ParseDuration(timeoutStr)
	if err != nil {
		return fmt.Errorf("invalid timeout: %w", err)
	}
	ctx, cancel := signalContext()
	defer cancel()

	prov, err := schema.NewProvider(ctx, schema.Options{
		ProtoFiles:    protoFiles,
		ImportPaths:   importPaths,
		UseReflection: useReflection,
		Address:       targetAddr,
		Insecure:      targetInsecure,
		Timeout:       timeout,
	})
	if err != nil {
		return fmt.Errorf("init schema provider: %w", err)
	}
	defer prov.Close()

	p, err := proxy.New(ctx, proxy.Options{
		ListenAddr:     listenAddr,
		TargetAddr:     targetAddr,
		TargetInsecure: targetInsecure,
		Schema:         prov,
		Timeout:        timeout,
	})
	if err != nil {
		return err
	}
	defer p.Close()

	sess := p.Session()
	sess.UseReflect = useReflection
	sess.ProtoFiles = protoFiles
	sess.ImportPaths = importPaths

	go func() {
		<-ctx.Done()
		if err := session.Save(outputFile, p.Session()); err != nil {
			fmt.Fprintf(os.Stderr, "failed to save session: %v\n", err)
		}
	}()

	fmt.Printf("Recording proxy listening on %s -> upstream %s (Ctrl+C to stop)\n", listenAddr, targetAddr)
	if err := p.Run(ctx); err != nil && ctx.Err() == nil {
		return err
	}
	if err := session.Save(outputFile, p.Session()); err != nil {
		return fmt.Errorf("save session: %w", err)
	}
	fmt.Printf("Session saved to %s (entries=%d)\n", outputFile, len(sess.Entries))
	return nil
}

func runReplay() error {
	sess, err := session.Load(sessionFile)
	if err != nil {
		return err
	}
	ctx, cancel := signalContext()
	defer cancel()

	result, err := replay.Run(ctx, replay.Options{
		Session:      sess,
		ExtraIgnores: ignoreFields,
	})
	if err != nil {
		return err
	}
	for _, e := range result.Entries {
		status := "PASS"
		if e.Error != "" {
			status = "ERROR"
		} else if !e.Passed {
			status = "DIFF"
		}
		fmt.Printf("[%s] %s (%s/%s) - %v\n", status, e.ID, e.Service, e.Method, e.Duration)
		if e.Error != "" {
			fmt.Printf("  error: %s\n", e.Error)
			continue
		}
		if !e.Passed {
			fmt.Printf("  diff:\n%s", indent(diffFormat(e), "    "))
		}
	}
	fmt.Printf("\n=== Replay Summary ===\n")
	fmt.Printf("Total: %d  Passed: %d  Failed: %d  Error: %d  Success: %.1f%%\n",
		result.Stats.Total, result.Stats.Passed, result.Stats.Failed,
		result.Stats.Error, result.Stats.SuccessRate)
	if result.Stats.Failed > 0 || result.Stats.Error > 0 {
		os.Exit(1)
	}
	return nil
}

func indent(s, prefix string) string {
	out := ""
	for _, line := range splitLines(s) {
		if line == "" {
			out += "\n"
			continue
		}
		out += prefix + line + "\n"
	}
	return out
}

func splitLines(s string) []string {
	var out []string
	start := 0
	for i := 0; i < len(s); i++ {
		if s[i] == '\n' {
			out = append(out, s[start:i])
			start = i + 1
		}
	}
	if start < len(s) {
		out = append(out, s[start:])
	}
	return out
}

func diffFormat(e replay.EntryResult) string {
	return diff.FormatUnified(e.Diff)
}

func signalContext() (context.Context, context.CancelFunc) {
	ctx, cancel := context.WithCancel(context.Background())
	ch := make(chan os.Signal, 1)
	signal.Notify(ch, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-ch
		cancel()
	}()
	return ctx, cancel
}
