package api

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/gin-gonic/gin"

	"greenhouse-sim/config"
	"greenhouse-sim/influx"
	"greenhouse-sim/model"
)

const (
	alertThresholdTempHigh = 35.0
	alertThresholdHumidLow = 40.0
	alertDurationSeconds   = 30
)

type alertTracker struct {
	mu       sync.Mutex
	active   map[string]*activeAlert
	history  map[string]model.AlertEvent
}

type activeAlert struct {
	alertID     string
	zoneID      string
	zoneName    string
	alertType   model.AlertType
	startTime   int64
	firstValue  float64
	lastValue   float64
	triggerTime int64
}

func newAlertTracker() *alertTracker {
	return &alertTracker{
		active:  make(map[string]*activeAlert),
		history: make(map[string]model.AlertEvent),
	}
}

type Server struct {
	store       *influx.Store
	engine      *gin.Engine
	wsMgr       *wsManager
	cfg         config.Config
	mqttCli     mqtt.Client
	alertTrack  *alertTracker
}

func New(cfg config.Config, store *influx.Store) *Server {
	gin.SetMode(gin.ReleaseMode)
	r := gin.New()
	r.Use(gin.Recovery())
	r.Use(corsMiddleware())

	s := &Server{
		store:      store,
		engine:     r,
		cfg:        cfg,
		wsMgr:      newWsManager(),
		alertTrack: newAlertTracker(),
	}
	s.register()

	go s.startMQTTConsumer()

	return s
}

func corsMiddleware() gin.HandlerFunc {
	return func(c *gin.Context) {
		c.Writer.Header().Set("Access-Control-Allow-Origin", "*")
		c.Writer.Header().Set("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS")
		c.Writer.Header().Set("Access-Control-Allow-Headers", "Content-Type,Authorization")
		if c.Request.Method == "OPTIONS" {
			c.AbortWithStatus(http.StatusNoContent)
			return
		}
		c.Next()
	}
}

func (s *Server) register() {
	s.engine.GET("/api/zones", s.zones)
	s.engine.GET("/api/history", s.history)
	s.engine.GET("/api/alerts", s.alerts)
	s.engine.GET("/api/realtime", s.realtimeWS)
	s.engine.GET("/api/health", func(c *gin.Context) {
		c.JSON(200, gin.H{"status": "ok"})
	})
}

func (s *Server) zones(c *gin.Context) {
	c.JSON(200, gin.H{"zones": model.Zones})
}

func parseRange(r string) time.Duration {
	switch r {
	case "1h":
		return time.Hour
	case "24h":
		return 24 * time.Hour
	case "5m":
		return 5 * time.Minute
	default:
		return 5 * time.Minute
	}
}

func (s *Server) history(c *gin.Context) {
	zoneID := c.Query("zone_id")
	rng := c.DefaultQuery("range", "5m")
	metric := c.DefaultQuery("metric", "")
	if zoneID == "" {
		c.JSON(400, gin.H{"error": "zone_id required"})
		return
	}
	records, err := s.store.QueryRange(zoneID, parseRange(rng), metric)
	if err != nil {
		c.JSON(500, gin.H{"error": err.Error()})
		return
	}
	c.JSON(200, gin.H{"zone_id": zoneID, "range": rng, "records": records})
}

func (s *Server) alerts(c *gin.Context) {
	zoneID := c.DefaultQuery("zone_id", "")
	rng := c.DefaultQuery("range", "24h")
	events, err := s.store.QueryAlerts(zoneID, parseRange(rng))
	if err != nil {
		c.JSON(500, gin.H{"error": err.Error()})
		return
	}
	c.JSON(200, gin.H{"alerts": events, "zone_id": zoneID, "range": rng})
}

func (s *Server) Run() error {
	return s.engine.Run(s.cfg.HTTPAddr)
}

func (s *Server) checkAndUpdateAlert(r model.SensorReading) {
	now := time.Now().Unix()

	s.alertTrack.mu.Lock()
	defer s.alertTrack.mu.Unlock()

	checks := []struct {
		alertType model.AlertType
		trigger   bool
		value     float64
		message   string
	}{
		{
			alertType: model.AlertTypeTempHigh,
			trigger:   r.Temperature > alertThresholdTempHigh,
			value:     r.Temperature,
			message:   fmt.Sprintf("温度过高 (%.1f°C > %.1f°C)", r.Temperature, alertThresholdTempHigh),
		},
		{
			alertType: model.AlertTypeHumidLow,
			trigger:   r.Humidity < alertThresholdHumidLow,
			value:     r.Humidity,
			message:   fmt.Sprintf("湿度过低 (%.1f%% < %.1f%%)", r.Humidity, alertThresholdHumidLow),
		},
	}

	for _, chk := range checks {
		key := r.ZoneID + "_" + string(chk.alertType)

		if chk.trigger {
			if active, ok := s.alertTrack.active[key]; ok {
				active.lastValue = chk.value

				if now-active.startTime >= alertDurationSeconds && active.triggerTime == 0 {
					active.triggerTime = now
					evt := model.AlertEvent{
						ID:        active.alertID,
						ZoneID:    r.ZoneID,
						ZoneName:  r.ZoneName,
						Type:      chk.alertType,
						Message:   chk.message,
						StartTime: active.startTime,
						EndTime:   now,
						Value:     chk.value,
						Resolved:  false,
					}
					s.store.WriteAlert(evt)
					s.wsMgr.broadcastAlert(evt)
					log.Printf("[ALERT] %s [%s]: %s", r.ZoneName, chk.alertType, chk.message)
				}
			} else {
				alertID := fmt.Sprintf("alert-%d-%s-%s", now, r.ZoneID, chk.alertType)
				s.alertTrack.active[key] = &activeAlert{
					alertID:    alertID,
					zoneID:     r.ZoneID,
					zoneName:   r.ZoneName,
					alertType:  chk.alertType,
					startTime:  now,
					firstValue: chk.value,
					lastValue:  chk.value,
				}
			}
		} else {
			if active, ok := s.alertTrack.active[key]; ok {
				if now-active.startTime >= alertDurationSeconds {
					evt := model.AlertEvent{
						ID:        active.alertID,
						ZoneID:    r.ZoneID,
						ZoneName:  r.ZoneName,
						Type:      chk.alertType,
						Message:   fmt.Sprintf("已恢复 (结束持续 %ds)", now-active.startTime),
						StartTime: active.startTime,
						EndTime:   now,
						Value:     active.lastValue,
						Resolved:  true,
					}
					s.store.WriteAlert(evt)
					s.wsMgr.broadcastAlert(evt)
					log.Printf("[RESOLVED] %s [%s]", r.ZoneName, chk.alertType)
				}
				delete(s.alertTrack.active, key)
			}
		}
	}
}

func (s *Server) startMQTTConsumer() {
	opts := mqtt.NewClientOptions().
		AddBroker(s.cfg.MQTTBroker).
		SetClientID("greenhouse_sim_ws_consumer").
		SetAutoReconnect(true).
		SetCleanSession(true)
	if s.cfg.MQTTUser != "" {
		opts.SetUsername(s.cfg.MQTTUser)
		opts.SetPassword(s.cfg.MQTTPass)
	}
	cli := mqtt.NewClient(opts)
	token := cli.Connect()
	token.WaitTimeout(5 * time.Second)
	if err := token.Error(); err != nil {
		log.Printf("mqtt consumer connect err: %v", err)
		return
	}
	s.mqttCli = cli

	var (
		dedupMu     sync.Mutex
		dedupMap    = make(map[string]int64)
		dedupWindow = int64(300)
		lastCleanup = time.Now().Unix()
	)

	handler := func(_ mqtt.Client, m mqtt.Message) {
		var r model.SensorReading
		if err := json.Unmarshal(m.Payload(), &r); err != nil {
			return
		}

		dedupMu.Lock()
		key := r.ZoneID + "_" + time.Unix(r.Timestamp, 0).Format(time.RFC3339)
		if ts, exists := dedupMap[key]; exists && time.Now().Unix()-ts < dedupWindow {
			dedupMu.Unlock()
			log.Printf("dedup: skipped duplicate %s", key)
			return
		}
		dedupMap[key] = time.Now().Unix()
		now := time.Now().Unix()
		if now-lastCleanup > dedupWindow {
			for k, v := range dedupMap {
				if now-v > dedupWindow {
					delete(dedupMap, k)
				}
			}
			lastCleanup = now
		}
		dedupMu.Unlock()

		s.checkAndUpdateAlert(r)
		s.wsMgr.broadcastSensor(r)
	}

	if token := cli.Subscribe("greenhouse/+/sensors", 0, handler); token.Wait() && token.Error() != nil {
		log.Printf("mqtt subscribe err: %v", token.Error())
	} else {
		log.Println("mqtt consumer subscribed to greenhouse/+/sensors")
	}
}
