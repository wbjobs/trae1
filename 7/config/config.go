package config

import (
	"log"

	"github.com/spf13/viper"
)

type Config struct {
	Server   ServerConfig   `mapstructure:"server"`
	Redis    RedisConfig    `mapstructure:"redis"`
	Security SecurityConfig `mapstructure:"security"`
	Log      LogConfig      `mapstructure:"log"`
}

type ServerConfig struct {
	Port         string `mapstructure:"port"`
	ReadTimeout  int    `mapstructure:"read_timeout"`
	WriteTimeout int    `mapstructure:"write_timeout"`
}

type RedisConfig struct {
	Host     string `mapstructure:"host"`
	Port     int    `mapstructure:"port"`
	Password string `mapstructure:"password"`
	DB       int    `mapstructure:"db"`
	PoolSize int    `mapstructure:"pool_size"`
}

type SecurityConfig struct {
	SignatureExpire int      `mapstructure:"signature_expire"`
	TimestampTolerance int  `mapstructure:"timestamp_tolerance"`
	NonceExpire     int      `mapstructure:"nonce_expire"`
	AllowedClients  []ClientConfig `mapstructure:"allowed_clients"`
	IPWhitelist     []string `mapstructure:"ip_whitelist"`
	IPBlacklist     []string `mapstructure:"ip_blacklist"`
}

type ClientConfig struct {
	ClientID     string   `mapstructure:"client_id"`
	ClientSecret string   `mapstructure:"client_secret"`
	Permissions  []string `mapstructure:"permissions"`
	RateLimit    int      `mapstructure:"rate_limit"`
}

type LogConfig struct {
	Level    string `mapstructure:"level"`
	FilePath string `mapstructure:"file_path"`
	MaxSize  int    `mapstructure:"max_size"`
	MaxAge   int    `mapstructure:"max_age"`
}

var AppConfig *Config

func LoadConfig() {
	viper.SetConfigName("config")
	viper.SetConfigType("yaml")
	viper.AddConfigPath(".")
	viper.AddConfigPath("./config")

	viper.SetDefault("server.port", "8080")
	viper.SetDefault("server.read_timeout", 10)
	viper.SetDefault("server.write_timeout", 10)

	viper.SetDefault("redis.host", "localhost")
	viper.SetDefault("redis.port", 6379)
	viper.SetDefault("redis.password", "")
	viper.SetDefault("redis.db", 0)
	viper.SetDefault("redis.pool_size", 10)

	viper.SetDefault("security.signature_expire", 300)
	viper.SetDefault("security.timestamp_tolerance", 300)
	viper.SetDefault("security.nonce_expire", 600)

	viper.SetDefault("log.level", "info")
	viper.SetDefault("log.file_path", "./logs/app.log")
	viper.SetDefault("log.max_size", 100)
	viper.SetDefault("log.max_age", 30)

	if err := viper.ReadInConfig(); err != nil {
		log.Printf("Warning: Config file not found, using defaults: %v", err)
	}

	AppConfig = &Config{}
	if err := viper.Unmarshal(AppConfig); err != nil {
		log.Fatalf("Failed to unmarshal config: %v", err)
	}

	log.Println("Configuration loaded successfully")
}

func GetClientConfig(clientID string) *ClientConfig {
	for i := range AppConfig.Security.AllowedClients {
		if AppConfig.Security.AllowedClients[i].ClientID == clientID {
			return &AppConfig.Security.AllowedClients[i]
		}
	}
	return nil
}

func GetAllClientIDs() []string {
	var ids []string
	for _, client := range AppConfig.Security.AllowedClients {
		ids = append(ids, client.ClientID)
	}
	return ids
}
