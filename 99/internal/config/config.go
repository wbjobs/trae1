package config

import "time"

type Config struct {
	EtcdEndpoints   []string      `json:"etcd_endpoints"`
	EtcdPrefix      string        `json:"etcd_prefix"`
	ListenAddr      string        `json:"listen_addr"`
	BaseExportPath  string        `json:"base_export_path"`
	AlertThreshold  float64       `json:"alert_threshold"`
	WebhookURL      string        `json:"webhook_url"`
	GaneshaDBusBus  string        `json:"ganesha_dbus_bus"`
	SyncInterval    time.Duration `json:"sync_interval"`
	InotifyBufSize  int           `json:"inotify_buf_size"`
	InotifyChanSize int           `json:"inotify_chan_size"`
	LogLevel        string        `json:"log_level"`
}

func Default() *Config {
	return &Config{
		EtcdEndpoints:   []string{"127.0.0.1:2379"},
		EtcdPrefix:      "/quotad/",
		ListenAddr:      ":8080",
		BaseExportPath:  "/nfs/exports",
		AlertThreshold:  0.85,
		WebhookURL:      "",
		GaneshaDBusBus:  "system",
		SyncInterval:    24 * time.Hour,
		InotifyBufSize:  65536,
		InotifyChanSize: 4096,
		LogLevel:        "info",
	}
}
