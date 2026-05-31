package config

type Config struct {
	HTTPAddr   string
	MQTTBroker string
	MQTTClient string
	MQTTUser   string
	MQTTPass   string
	InfluxURL  string
	InfluxOrg  string
	InfluxTok  string
	InfluxBucket string
}

var Default = Config{
	HTTPAddr:     ":8080",
	MQTTBroker:   "tcp://127.0.0.1:1883",
	MQTTClient:   "greenhouse_sim_publisher",
	MQTTUser:     "",
	MQTTPass:     "",
	InfluxURL:    "http://127.0.0.1:8086",
	InfluxOrg:    "greenhouse",
	InfluxTok:    "dev-token",
	InfluxBucket: "greenhouse",
}
