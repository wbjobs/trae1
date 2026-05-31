package config

import (
	"fmt"
	"os"

	"gopkg.in/yaml.v3"
)

type Peer struct {
	Name     string `yaml:"name"`
	ASN      uint32 `yaml:"asn"`
	Address  string `yaml:"address"`
	Port     uint16 `yaml:"port"`
	Priority int    `yaml:"priority"`
}

type Local struct {
	ASN        uint32 `yaml:"asn"`
	RouterID   string `yaml:"router_id"`
	ListenHost string `yaml:"listen_host"`
	ListenPort uint16 `yaml:"listen_port"`
}

type ThreatCommunity struct {
	BaseASN uint32 `yaml:"base_asn"`
}

type Redis struct {
	Addr      string `yaml:"addr"`
	Password  string `yaml:"password"`
	DB        int    `yaml:"db"`
	KeyPrefix string `yaml:"key_prefix"`
}

type HTTP struct {
	Listen string `yaml:"listen"`
}

type Sync struct {
	StartupWait int `yaml:"startup_wait"`
}

type ML struct {
	Enabled         bool    `yaml:"enabled"`
	ModelPath       string  `yaml:"model_path"`
	BGPWeight       float64 `yaml:"bgp_weight"`
	FeatureWindow   int     `yaml:"feature_window"`
	PathCategories  int     `yaml:"path_categories"`
	MinSamples      int     `yaml:"min_samples"`
}

type Config struct {
	Local           Local           `yaml:"local"`
	Peers           []Peer          `yaml:"peers"`
	ThreatCommunity ThreatCommunity `yaml:"threat_community"`
	Redis           Redis           `yaml:"redis"`
	HTTP            HTTP            `yaml:"http"`
	Sync            Sync            `yaml:"sync"`
	ML              ML              `yaml:"ml"`
}

func Load(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read config: %w", err)
	}
	var c Config
	if err := yaml.Unmarshal(data, &c); err != nil {
		return nil, fmt.Errorf("parse config: %w", err)
	}
	if c.Redis.KeyPrefix == "" {
		c.Redis.KeyPrefix = "iprep:"
	}
	if c.HTTP.Listen == "" {
		c.HTTP.Listen = ":8080"
	}
	if c.Sync.StartupWait <= 0 {
		c.Sync.StartupWait = 30
	}
	if c.Local.ListenPort == 0 {
		c.Local.ListenPort = 179
	}
	if c.ML.BGPWeight <= 0 || c.ML.BGPWeight > 1 {
		c.ML.BGPWeight = 0.6
	}
	if c.ML.FeatureWindow <= 0 {
		c.ML.FeatureWindow = 3600
	}
	if c.ML.PathCategories <= 0 {
		c.ML.PathCategories = 8
	}
	if c.ML.MinSamples <= 0 {
		c.ML.MinSamples = 10
	}
	return &c, nil
}
