package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"strings"
	"syscall"

	"raftkv/internal/cluster"
	"raftkv/internal/httpapi"
	"raftkv/internal/store"
)

func main() {
	var (
		nodeID      = flag.String("id", "node1", "Raft node ID")
		raftAddr    = flag.String("raft", "127.0.0.1:7001", "Raft bind address")
		httpAddr    = flag.String("http", "127.0.0.1:8001", "HTTP API bind address")
		dataDir     = flag.String("data", "./data-node1", "Data directory (LevelDB + Raft logs)")
		bootstrap   = flag.Bool("bootstrap", false, "Bootstrap a new cluster as the initial leader")
		join        = flag.String("join", "", "HTTP address of an existing node to join (comma separated)")
		initialPeers = flag.String("peers", "", "Initial peers: id1=addr1,id2=addr2 (for multi-node bootstrap)")
	)
	flag.Parse()

	log.SetFlags(log.LstdFlags | log.Lmicroseconds)
	log.SetPrefix(fmt.Sprintf("[%s] ", *nodeID))

	kv := store.NewLevelDBStore(*dataDir + "/kv")
	if err := kv.Open(); err != nil {
		log.Fatalf("failed to open kv store: %v", err)
	}
	defer kv.Close()

	node, err := cluster.NewNode(cluster.Options{
		NodeID:   *nodeID,
		BindAddr: *raftAddr,
		DataDir:  *dataDir + "/raft",
		Store:    kv,
	})
	if err != nil {
		log.Fatalf("failed to create raft node: %v", err)
	}

	if *bootstrap {
		var peers map[string]string
		if *initialPeers != "" {
			peers = parsePeers(*initialPeers)
		}
		if err := node.Bootstrap(peers); err != nil {
			log.Fatalf("failed to bootstrap raft: %v", err)
		}
	} else {
		if err := node.Start(); err != nil {
			log.Fatalf("failed to start raft: %v", err)
		}
	}

	if *join != "" {
		addrs := strings.Split(*join, ",")
		for _, a := range addrs {
			a = strings.TrimSpace(a)
			if a == "" {
				continue
			}
			if err := node.JoinViaHTTP(a, *nodeID, *raftAddr); err != nil {
				log.Printf("join via %s failed: %v (will retry or proceed)", a, err)
			} else {
				log.Printf("successfully joined cluster via %s", a)
				break
			}
		}
	}

	srv := httpapi.New(*httpAddr, node, kv)
	go func() {
		log.Printf("HTTP API listening on %s", *httpAddr)
		if err := srv.ListenAndServe(); err != nil {
			log.Printf("http server stopped: %v", err)
		}
	}()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh

	log.Println("shutting down...")
	_ = srv.Close()
	_ = node.Stop()
}

func parsePeers(s string) map[string]string {
	out := map[string]string{}
	for _, p := range strings.Split(s, ",") {
		p = strings.TrimSpace(p)
		if p == "" {
			continue
		}
		kv := strings.SplitN(p, "=", 2)
		if len(kv) == 2 {
			out[strings.TrimSpace(kv[0])] = strings.TrimSpace(kv[1])
		}
	}
	return out
}
