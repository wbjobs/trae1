package influx

import (
	"context"
	"fmt"
	"log"
	"strings"
	"time"

	"github.com/influxdata/influxdb-client-go/v2"
	"github.com/influxdata/influxdb-client-go/v2/api"
	"github.com/influxdata/influxdb-client-go/v2/api/query"

	"greenhouse-sim/config"
	"greenhouse-sim/model"
)

type Store struct {
	client influxdb2.Client
	write  api.WriteAPI
	query  api.QueryAPI
	org    string
	bucket string
}

func New(cfg config.Config) *Store {
	client := influxdb2.NewClientWithOptions(cfg.InfluxURL, cfg.InfluxTok,
		influxdb2.DefaultOptions().SetBatchSize(20))
	return &Store{
		client: client,
		write:  client.WriteAPI(cfg.InfluxOrg, cfg.InfluxBucket),
		query:  client.QueryAPI(cfg.InfluxOrg),
		org:    cfg.InfluxOrg,
		bucket: cfg.InfluxBucket,
	}
}

func (s *Store) Write(r model.SensorReading) {
	p := influxdb2.NewPointWithMeasurement("sensors").
		AddTag("zone_id", r.ZoneID).
		AddTag("zone_name", r.ZoneName).
		AddField("temperature", r.Temperature).
		AddField("humidity", r.Humidity).
		AddField("light", r.Light).
		AddField("co2", r.CO2).
		SetTime(time.Unix(r.Timestamp, 0))
	s.write.WritePoint(p)
}

func durationToFlux(d time.Duration) string {
	secs := int64(d.Seconds())
	if secs%3600 == 0 {
		return fmt.Sprintf("%dh", secs/3600)
	}
	if secs%60 == 0 {
		return fmt.Sprintf("%dm", secs/60)
	}
	return fmt.Sprintf("%ds", secs)
}

func (s *Store) QueryRange(zoneID string, start time.Duration, metric string) ([]model.SensorReading, error) {
	parts := []string{
		fmt.Sprintf(`from(bucket: "%s")`, s.bucket),
		fmt.Sprintf(`|> range(start: -%s)`, durationToFlux(start)),
		fmt.Sprintf(`|> filter(fn: (r) => r._measurement == "sensors" and r.zone_id == "%s")`, zoneID),
	}
	if metric != "" {
		parts = append(parts, fmt.Sprintf(`|> filter(fn: (r) => r._field == "%s")`, metric))
	}
	parts = append(parts,
		`|> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")`,
		`|> keep(columns:["_time","zone_id","zone_name","temperature","humidity","light","co2"])`,
		`|> sort(columns:["_time"])`,
	)
	flux := strings.Join(parts, "\n  ")
	result, err := s.query.Query(context.Background(), flux)
	if err != nil {
		return nil, err
	}
	var out []model.SensorReading
	for result.Next() {
		rec := result.Record()
		out = append(out, recordToReading(rec))
	}
	return out, result.Err()
}

func recordToReading(rec *query.FluxRecord) model.SensorReading {
	r := model.SensorReading{
		ZoneID:   getString(rec.ValueByKey("zone_id")),
		ZoneName: getString(rec.ValueByKey("zone_name")),
	}
	if t, ok := rec.Time().(time.Time); ok {
		r.Timestamp = t.Unix()
	} else {
		r.Timestamp = rec.Time().(time.Time).Unix()
	}
	if v, ok := rec.ValueByKey("temperature").(float64); ok {
		r.Temperature = v
	}
	if v, ok := rec.ValueByKey("humidity").(float64); ok {
		r.Humidity = v
	}
	if v, ok := rec.ValueByKey("light").(float64); ok {
		r.Light = v
	}
	if v, ok := rec.ValueByKey("co2").(float64); ok {
		r.CO2 = v
	}
	return r
}

func getString(v interface{}) string {
	if v == nil {
		return ""
	}
	if s, ok := v.(string); ok {
		return s
	}
	return ""
}

func (s *Store) Close() {
	s.client.Close()
	log.Println("influx client closed")
}

func (s *Store) WriteAlert(a model.AlertEvent) {
	p := influxdb2.NewPointWithMeasurement("alerts").
		AddTag("zone_id", a.ZoneID).
		AddTag("zone_name", a.ZoneName).
		AddTag("type", string(a.Type)).
		AddTag("alert_id", a.ID).
		AddField("message", a.Message).
		AddField("start_time", a.StartTime).
		AddField("end_time", a.EndTime).
		AddField("value", a.Value).
		AddField("resolved", a.Resolved).
		SetTime(time.Unix(a.StartTime, 0))
	s.write.WritePoint(p)
}

func (s *Store) QueryAlerts(zoneID string, start time.Duration) ([]model.AlertEvent, error) {
	var parts []string
	parts = append(parts,
		fmt.Sprintf(`from(bucket: "%s")`, s.bucket),
		fmt.Sprintf(`|> range(start: -%s)`, durationToFlux(start)),
		`|> filter(fn: (r) => r._measurement == "alerts")`,
	)
	if zoneID != "" {
		parts = append(parts, fmt.Sprintf(`|> filter(fn: (r) => r.zone_id == "%s")`, zoneID))
	}
	parts = append(parts,
		`|> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")`,
		`|> keep(columns:["_time","alert_id","zone_id","zone_name","type","message","start_time","end_time","value","resolved"])`,
		`|> sort(columns:["_time"], desc: true)`,
	)
	flux := strings.Join(parts, "\n  ")
	result, err := s.query.Query(context.Background(), flux)
	if err != nil {
		return nil, err
	}
	var out []model.AlertEvent
	for result.Next() {
		rec := result.Record()
		out = append(out, recordToAlert(rec))
	}
	return out, result.Err()
}

func recordToAlert(rec *query.FluxRecord) model.AlertEvent {
	a := model.AlertEvent{
		ID:       getString(rec.ValueByKey("alert_id")),
		ZoneID:   getString(rec.ValueByKey("zone_id")),
		ZoneName: getString(rec.ValueByKey("zone_name")),
		Type:     model.AlertType(getString(rec.ValueByKey("type"))),
		Message:  getString(rec.ValueByKey("message")),
	}
	if v, ok := rec.ValueByKey("start_time").(float64); ok {
		a.StartTime = int64(v)
	}
	if v, ok := rec.ValueByKey("end_time").(float64); ok {
		a.EndTime = int64(v)
	}
	if v, ok := rec.ValueByKey("value").(float64); ok {
		a.Value = v
	}
	if v, ok := rec.ValueByKey("resolved").(bool); ok {
		a.Resolved = v
	}
	return a
}
