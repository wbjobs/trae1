package model

type SensorReading struct {
	ZoneID      string  `json:"zone_id"`
	ZoneName    string  `json:"zone_name"`
	Timestamp   int64   `json:"timestamp"`
	Temperature float64 `json:"temperature"`
	Humidity    float64 `json:"humidity"`
	Light       float64 `json:"light"`
	CO2         float64 `json:"co2"`
}

type AlertType string

const (
	AlertTypeTempHigh  AlertType = "temp_high"
	AlertTypeHumidLow  AlertType = "humid_low"
)

type AlertEvent struct {
	ID         string    `json:"id"`
	ZoneID     string    `json:"zone_id"`
	ZoneName   string    `json:"zone_name"`
	Type       AlertType `json:"type"`
	Message    string    `json:"message"`
	StartTime  int64     `json:"start_time"`
	EndTime    int64     `json:"end_time"`
	Value      float64   `json:"value"`
	Resolved   bool      `json:"resolved"`
}

type WSMessage struct {
	Type string      `json:"type"`
	Data interface{} `json:"data"`
}

type Zone struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

var Zones = []Zone{
	{ID: "zone-1", Name: "番茄种植区"},
	{ID: "zone-2", Name: "叶菜种植区"},
	{ID: "zone-3", Name: "育苗区"},
	{ID: "zone-4", Name: "草莓种植区"},
	{ID: "zone-5", Name: "花卉培育区"},
}

var ZoneMap = map[string]Zone{}

func init() {
	for _, z := range Zones {
		ZoneMap[z.ID] = z
	}
}
