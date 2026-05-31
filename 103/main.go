package main

import (
	"flag"
	"log"
	"os"
	"os/signal"
	"syscall"

	"mongochsync/config"
	"mongochsync/syncer"
)

func main() {
	configPath := flag.String("config", "config.yaml", "Path to config file")
	initialSync := flag.Bool("initial-sync", false, "Perform initial full sync")
	status := flag.Bool("status", false, "Print sync status")
	resume := flag.Bool("resume", false, "Manual resume after election (for manual resume policy)")
	reloadPipelines := flag.Bool("reload-pipelines", false, "Hot reload ETL pipelines without interrupting sync")
	flag.Parse()

	cfg, err := config.LoadConfig(*configPath)
	if err != nil {
		log.Fatalf("Failed to load config: %v", err)
	}

	sync, err := syncer.New(cfg)
	if err != nil {
		log.Fatalf("Failed to create syncer: %v", err)
	}
	defer sync.Close()

	if *status {
		sync.PrintStatus()
		return
	}

	if *resume {
		if err := sync.ManualResume(); err != nil {
			log.Fatalf("Manual resume failed: %v", err)
		}
		log.Println("Manual resume completed successfully")
		return
	}

	if *reloadPipelines {
		if err := sync.ReloadPipelines(); err != nil {
			log.Fatalf("Reload pipelines failed: %v", err)
		}
		log.Println("Pipelines reloaded successfully")
		return
	}

	if *initialSync {
		if err := sync.InitialSync(); err != nil {
			log.Fatalf("Initial sync failed: %v", err)
		}
	}

	go func() {
		if err := sync.Start(); err != nil {
			log.Fatalf("Sync failed: %v", err)
		}
	}()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	<-sigChan

	log.Println("Shutting down...")
}
