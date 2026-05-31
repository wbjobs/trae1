package config

import (
	"fmt"
	"strings"

	"github.com/spf13/viper"
)

type MQType string

const (
	MQTypeRocketMQ MQType = "rocketmq"
	MQTypeTDMQ     MQType = "tdmq"
)

type Mapping struct {
	Source        MQType `mapstructure:"source"`
	SourceTopic   string `mapstructure:"source_topic"`
	Target        MQType `mapstructure:"target"`
	TargetTopic   string `mapstructure:"target_topic"`
	SourceFormat  string `mapstructure:"source_format"`
	TargetFormat  string `mapstructure:"target_format"`
	SourceSchema  string `mapstructure:"source_schema"`
	TargetSchema  string `mapstructure:"target_schema"`
}

type NATSConfig struct {
	URL           string `mapstructure:"url"`
	SubjectPrefix string `mapstructure:"subject_prefix"`
}

type RocketMQConfig struct {
	NameServer string   `mapstructure:"name_server"`
	AccessKey  string   `mapstructure:"access_key"`
	SecretKey  string   `mapstructure:"secret_key"`
	Namespace  string   `mapstructure:"namespace"`
	InstanceID string   `mapstructure:"instance_id"`
	Group      string   `mapstructure:"group"`
	Topics     []string `mapstructure:"topics"`
}

type TDMQConfig struct {
	Endpoints        string   `mapstructure:"endpoints"`
	AuthToken        string   `mapstructure:"auth_token"`
	Cluster          string   `mapstructure:"cluster"`
	Tenant           string   `mapstructure:"tenant"`
	Namespace        string   `mapstructure:"namespace"`
	Subscription     string   `mapstructure:"subscription"`
	Topics           []string `mapstructure:"topics"`
	RateLimitPerSec  int      `mapstructure:"rate_limit_per_sec"`
	RateLimitWindowMs int64   `mapstructure:"rate_limit_window_ms"`
	BackoffInitialMs int64    `mapstructure:"backoff_initial_ms"`
	BackoffMaxMs     int64    `mapstructure:"backoff_max_ms"`
}

type KafkaConfig struct {
	Brokers       []string `mapstructure:"brokers"`
	DLQTopic      string   `mapstructure:"dlq_topic"`
	ConsumerGroup string   `mapstructure:"consumer_group"`
}

type RetryConfig struct {
	MaxAttempts  int `mapstructure:"max_attempts"`
	BackoffBaseMs int64 `mapstructure:"backoff_base_ms"`
	BackoffMaxMs  int64 `mapstructure:"backoff_max_ms"`
}

type HTTPConfig struct {
	Addr string `mapstructure:"addr"`
}

type AppConfig struct {
	Name     string `mapstructure:"name"`
	LogLevel string `mapstructure:"log_level"`
}

type TracingConfig struct {
	Enabled    bool   `mapstructure:"enabled"`
	JaegerURL  string `mapstructure:"jaeger_url"`
	ServiceName string `mapstructure:"service_name"`
	SampleRate float64 `mapstructure:"sample_rate"`
}

type SVIDConfig struct {
	Enabled  bool   `mapstructure:"enabled"`
	Audience string `mapstructure:"audience"`
	Issuer   string `mapstructure:"issuer"`
	JWKSURL  string `mapstructure:"jwks_url"`
}

type EtcdConfig struct {
	Endpoints []string `mapstructure:"endpoints"`
}

type Config struct {
	App      AppConfig      `mapstructure:"app"`
	NATS     NATSConfig     `mapstructure:"nats"`
	RocketMQ RocketMQConfig `mapstructure:"rocketmq"`
	TDMQ     TDMQConfig     `mapstructure:"tdmq"`
	Mappings []Mapping      `mapstructure:"mappings"`
	Kafka    KafkaConfig    `mapstructure:"kafka"`
	Retry    RetryConfig    `mapstructure:"retry"`
	HTTP     HTTPConfig     `mapstructure:"http"`
	Tracing  TracingConfig  `mapstructure:"tracing"`
	SVID     SVIDConfig     `mapstructure:"svid"`
	Etcd     EtcdConfig     `mapstructure:"etcd"`
}

func Load(path string) (*Config, error) {
	v := viper.New()
	v.SetConfigFile(path)
	v.SetConfigType("yaml")
	v.SetEnvPrefix("BRIDGE")
	v.SetEnvKeyReplacer(strings.NewReplacer(".", "_"))
	v.AutomaticEnv()

	if err := v.ReadInConfig(); err != nil {
		return nil, fmt.Errorf("read config: %w", err)
	}

	var c Config
	if err := v.Unmarshal(&c); err != nil {
		return nil, fmt.Errorf("unmarshal config: %w", err)
	}

	if c.Retry.MaxAttempts <= 0 {
		c.Retry.MaxAttempts = 5
	}
	if c.Retry.BackoffBaseMs <= 0 {
		c.Retry.BackoffBaseMs = 1000
	}
	if c.Retry.BackoffMaxMs <= 0 {
		c.Retry.BackoffMaxMs = 60000
	}
	if c.HTTP.Addr == "" {
		c.HTTP.Addr = ":8080"
	}
	if c.NATS.SubjectPrefix == "" {
		c.NATS.SubjectPrefix = "bridge"
	}
	if c.TDMQ.RateLimitPerSec <= 0 {
		c.TDMQ.RateLimitPerSec = 100
	}
	if c.TDMQ.RateLimitWindowMs <= 0 {
		c.TDMQ.RateLimitWindowMs = 1000
	}
	if c.TDMQ.BackoffInitialMs <= 0 {
		c.TDMQ.BackoffInitialMs = 1000
	}
	if c.TDMQ.BackoffMaxMs <= 0 {
		c.TDMQ.BackoffMaxMs = 60000
	}
	if c.Tracing.ServiceName == "" {
		c.Tracing.ServiceName = c.App.Name
	}
	if c.Tracing.JaegerURL == "" {
		c.Tracing.JaegerURL = "http://127.0.0.1:14268/api/traces"
	}
	if c.Tracing.SampleRate <= 0 {
		c.Tracing.SampleRate = 1.0
	}
	return &c, nil
}
