package main

type Config struct {
	RedisAddr    string `json:"redis_addr"`
	RedisPass    string `json:"redis_pass"`
	RedisDB      int    `json:"redis_db"`
	WSUrl        string `json:"ws_url"`
	HTTPAddr     string `json:"http_addr"`
	BatchSize    int    `json:"batch_size"`
	BatchInterval int   `json:"batch_interval_ms"`
	MockMode     bool   `json:"mock_mode"`
	MockStocks   int    `json:"mock_stocks"`
	MockTicksPerStock int `json:"mock_ticks_per_stock"`
}

func DefaultConfig() *Config {
	return &Config{
		RedisAddr:         "127.0.0.1:6379",
		RedisPass:         "",
		RedisDB:           0,
		WSUrl:             "ws://127.0.0.1:8765/ticks",
		HTTPAddr:          ":8080",
		BatchSize:         100,
		BatchInterval:     50,
		MockMode:          true,
		MockStocks:        2000,
		MockTicksPerStock: 2,
	}
}
