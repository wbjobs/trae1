package config

import (
	"fmt"
	"sync"

	"github.com/spf13/viper"
)

type Profile struct {
	Name         string `mapstructure:"name"`
	Resolution   string `mapstructure:"resolution"`
	VideoBitrate string `mapstructure:"video_bitrate"`
	AudioBitrate string `mapstructure:"audio_bitrate"`
	Preset       string `mapstructure:"preset"`
}

type StreamMonitor struct {
	Enabled            bool `mapstructure:"enabled"`
	DisconnectTimeout  int  `mapstructure:"disconnect_timeout"`
	ProbeInterval      int  `mapstructure:"probe_interval"`
	ProbeTimeout       int  `mapstructure:"probe_timeout"`
	MaxKeepalive       bool `mapstructure:"max_keepalive"`
}

type CGroups struct {
	Enabled        bool   `mapstructure:"enabled"`
	V1MountPath    string `mapstructure:"v1_mount_path"`
	V2MountPath    string `mapstructure:"v2_mount_path"`
	HighCPUShares  int    `mapstructure:"high_cpu_shares"`
	MediumCPUShares int   `mapstructure:"medium_cpu_shares"`
	LowCPUShares   int    `mapstructure:"low_cpu_shares"`
	HighCPUQuota   string `mapstructure:"high_cpu_quota"`
	MediumCPUQuota string `mapstructure:"medium_cpu_quota"`
	LowCPUQuota    string `mapstructure:"low_cpu_quota"`
}

type Queue struct {
	MaxConcurrent int `mapstructure:"max_concurrent"`
	HighReserved  int `mapstructure:"high_reserved_slots"`
	MediumReserved int `mapstructure:"medium_reserved_slots"`
}

type Config struct {
	Server struct {
		Host string `mapstructure:"host"`
		Port int    `mapstructure:"port"`
	} `mapstructure:"server"`

	RTMP struct {
		Enabled bool   `mapstructure:"enabled"`
		Listen  string `mapstructure:"listen"`
		App     string `mapstructure:"app"`
	} `mapstructure:"rtmp"`

	SRT struct {
		Enabled bool   `mapstructure:"enabled"`
		Listen  string `mapstructure:"listen"`
	} `mapstructure:"srt"`

	HLSOutput struct {
		Root            string `mapstructure:"root"`
		SegmentDuration int    `mapstructure:"segment_duration"`
		SegmentCount    int    `mapstructure:"segment_count"`
	} `mapstructure:"hls_output"`

	FFmpeg struct {
		Binary                 string `mapstructure:"binary"`
		MaxRetries             int    `mapstructure:"max_retries"`
		RetryInterval          int    `mapstructure:"retry_interval"`
		GracefulShutdownTimeout int   `mapstructure:"graceful_shutdown_timeout"`
	} `mapstructure:"ffmpeg"`

	Profiles      []Profile `mapstructure:"profiles"`
	Elasticsearch struct {
		Enabled   bool     `mapstructure:"enabled"`
		Addresses []string `mapstructure:"addresses"`
		Index     string   `mapstructure:"index"`
		Username  string   `mapstructure:"username"`
		Password  string   `mapstructure:"password"`
	} `mapstructure:"elasticsearch"`

	StreamMonitor         StreamMonitor `mapstructure:"stream_monitor"`
	ZombieCleanupInterval int           `mapstructure:"zombie_cleanup_interval"`
	CGroups               CGroups       `mapstructure:"cgroups"`
	Queue                 Queue         `mapstructure:"queue"`
}

var (
	cfg  *Config
	once sync.Once
)

func Load(path string) (*Config, error) {
	var err error
	once.Do(func() {
		v := viper.New()
		v.SetConfigFile(path)
		v.SetConfigType("yaml")
		if e := v.ReadInConfig(); e != nil {
			err = fmt.Errorf("读取配置失败: %w", e)
			return
		}
		c := &Config{}
		if e := v.Unmarshal(c); e != nil {
			err = fmt.Errorf("解析配置失败: %w", e)
			return
		}
		if c.StreamMonitor.DisconnectTimeout <= 0 {
			c.StreamMonitor.DisconnectTimeout = 30
		}
		if c.StreamMonitor.ProbeInterval <= 0 {
			c.StreamMonitor.ProbeInterval = 5
		}
		if c.StreamMonitor.ProbeTimeout <= 0 {
			c.StreamMonitor.ProbeTimeout = 5
		}
		if c.FFmpeg.GracefulShutdownTimeout <= 0 {
			c.FFmpeg.GracefulShutdownTimeout = 10
		}
		if c.ZombieCleanupInterval <= 0 {
			c.ZombieCleanupInterval = 60
		}
		if c.Queue.MaxConcurrent <= 0 {
			c.Queue.MaxConcurrent = 4
		}
		if c.Queue.HighReserved < 0 {
			c.Queue.HighReserved = 0
		}
		if c.Queue.MediumReserved < 0 {
			c.Queue.MediumReserved = 0
		}
		if c.CGroups.V1MountPath == "" {
			c.CGroups.V1MountPath = "/sys/fs/cgroup"
		}
		if c.CGroups.V2MountPath == "" {
			c.CGroups.V2MountPath = "/sys/fs/cgroup"
		}
		if c.CGroups.HighCPUShares <= 0 {
			c.CGroups.HighCPUShares = 1024
		}
		if c.CGroups.MediumCPUShares <= 0 {
			c.CGroups.MediumCPUShares = 512
		}
		if c.CGroups.LowCPUShares <= 0 {
			c.CGroups.LowCPUShares = 256
		}
		if c.CGroups.HighCPUQuota == "" {
			c.CGroups.HighCPUQuota = "-1"
		}
		if c.CGroups.MediumCPUQuota == "" {
			c.CGroups.MediumCPUQuota = "80%"
		}
		if c.CGroups.LowCPUQuota == "" {
			c.CGroups.LowCPUQuota = "40%"
		}
		cfg = c
	})
	return cfg, err
}

func Get() *Config { return cfg }

func (c *Config) Profile(name string) (Profile, bool) {
	for _, p := range c.Profiles {
		if p.Name == name {
			return p, true
		}
	}
	return Profile{}, false
}

func (c *Config) Addr() string {
	return fmt.Sprintf("%s:%d", c.Server.Host, c.Server.Port)
}
