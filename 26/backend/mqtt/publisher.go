package mqtt

import (
	"encoding/json"
	"log"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"

	"greenhouse-sim/config"
	"greenhouse-sim/model"
	"greenhouse-sim/simulator"
	"greenhouse-sim/influx"
)

type Publisher struct {
	client   mqtt.Client
	sim      *simulator.Simulator
	store    *influx.Store
	stopCh   chan struct{}
}

func New(cfg config.Config, store *influx.Store) (*Publisher, error) {
	opts := mqtt.NewClientOptions().
		AddBroker(cfg.MQTTBroker).
		SetClientID(cfg.MQTTClient).
		SetAutoReconnect(true).
		SetCleanSession(true).
		SetConnectionLostHandler(func(_ mqtt.Client, err error) {
			log.Printf("mqtt connection lost: %v", err)
		}).
		SetOnConnectHandler(func(c mqtt.Client) {
			log.Println("mqtt connected")
		})
	if cfg.MQTTUser != "" {
		opts.SetUsername(cfg.MQTTUser)
		opts.SetPassword(cfg.MQTTPass)
	}
	client := mqtt.NewClient(opts)
	token := client.Connect()
	token.WaitTimeout(5 * time.Second)
	if err := token.Error(); err != nil {
		return nil, err
	}
	return &Publisher{
		client: client,
		sim:    simulator.New(),
		store:  store,
		stopCh: make(chan struct{}),
	}, nil
}

func (p *Publisher) Start() {
	ticker := time.NewTicker(1 * time.Second)
	go func() {
		for {
			select {
			case <-ticker.C:
				p.publishAll()
			case <-p.stopCh:
				ticker.Stop()
				return
			}
		}
	}()
}

func (p *Publisher) publishAll() {
	now := time.Now()
	for _, z := range model.Zones {
		r := p.sim.Generate(z, now)
		topic := "greenhouse/" + z.ID + "/sensors"
		data, err := json.Marshal(r)
		if err != nil {
			log.Printf("marshal err: %v", err)
			continue
		}
		token := p.client.Publish(topic, 0, false, data)
		token.WaitTimeout(500 * time.Millisecond)
		if err := token.Error(); err != nil {
			log.Printf("mqtt publish err: %v", err)
		}
		if p.store != nil {
			p.store.Write(r)
		}
	}
}

func (p *Publisher) Stop() {
	close(p.stopCh)
	p.client.Disconnect(250)
}
