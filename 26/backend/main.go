package main

import (
	"log"
	"os"
	"os/signal"
	"syscall"

	"greenhouse-sim/api"
	"greenhouse-sim/config"
	"greenhouse-sim/influx"
	"greenhouse-sim/mqtt"
)

func main() {
	cfg := config.Default
	if v := os.Getenv("HTTP_ADDR"); v != "" {
		cfg.HTTPAddr = v
	}
	if v := os.Getenv("MQTT_BROKER"); v != "" {
		cfg.MQTTBroker = v
	}
	if v := os.Getenv("MQTT_USER"); v != "" {
		cfg.MQTTUser = v
	}
	if v := os.Getenv("MQTT_PASS"); v != "" {
		cfg.MQTTPass = v
	}
	if v := os.Getenv("INFLUX_URL"); v != "" {
		cfg.InfluxURL = v
	}
	if v := os.Getenv("INFLUX_ORG"); v != "" {
		cfg.InfluxOrg = v
	}
	if v := os.Getenv("INFLUX_TOKEN"); v != "" {
		cfg.InfluxTok = v
	}
	if v := os.Getenv("INFLUX_BUCKET"); v != "" {
		cfg.InfluxBucket = v
	}

	store := influx.New(cfg)
	defer store.Close()

	pub, err := mqtt.New(cfg, store)
	if err != nil {
		log.Printf("mqtt publisher init err: %v (realtime MQTT publish disabled, will retry)", err)
	} else {
		pub.Start()
		defer pub.Stop()
	}

	srv := api.New(cfg, store)

	go func() {
		log.Printf("http server listening on %s", cfg.HTTPAddr)
		if err := srv.Run(); err != nil {
			log.Fatalf("http server err: %v", err)
		}
	}()

	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	<-quit
	log.Println("shutdown...")
}
