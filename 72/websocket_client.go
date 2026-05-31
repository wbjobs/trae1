package main

import (
	"encoding/json"
	"log"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

type Tick struct {
	Code      string  `json:"code"`
	Price     float64 `json:"price"`
	Timestamp int64   `json:"timestamp"`
}

type WSClient struct {
	url       string
	conn      *websocket.Conn
	tickChan  chan *Tick
	mu        sync.RWMutex
	connected bool
	done      chan struct{}
}

func NewWSClient(url string, tickChan chan *Tick) *WSClient {
	return &WSClient{
		url:      url,
		tickChan: tickChan,
		done:     make(chan struct{}),
	}
}

func (w *WSClient) Connect() error {
	w.mu.Lock()
	defer w.mu.Unlock()

	if w.connected {
		return nil
	}

	dialer := websocket.Dialer{
		HandshakeTimeout: 10 * time.Second,
	}

	conn, _, err := dialer.Dial(w.url, nil)
	if err != nil {
		return err
	}

	w.conn = conn
	w.connected = true
	go w.readLoop()
	go w.heartbeat()

	log.Printf("WebSocket connected to %s", w.url)
	return nil
}

func (w *WSClient) readLoop() {
	defer func() {
		w.mu.Lock()
		w.connected = false
		w.mu.Unlock()
		close(w.done)
	}()

	for {
		_, msg, err := w.conn.ReadMessage()
		if err != nil {
			log.Printf("WebSocket read error: %v", err)
			return
		}

		var tick Tick
		if err := json.Unmarshal(msg, &tick); err != nil {
			log.Printf("WebSocket unmarshal error: %v", err)
			continue
		}

		if tick.Timestamp == 0 {
			tick.Timestamp = time.Now().UnixMilli()
		}

		select {
		case w.tickChan <- &tick:
		default:
			log.Printf("Tick channel full, dropping tick for %s", tick.Code)
		}
	}
}

func (w *WSClient) heartbeat() {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			w.mu.RLock()
			if w.connected && w.conn != nil {
				w.conn.WriteControl(websocket.PingMessage, []byte{}, time.Now().Add(5*time.Second))
			}
			w.mu.RUnlock()
		case <-w.done:
			return
		}
	}
}

func (w *WSClient) Reconnect() {
	w.mu.Lock()
	if w.conn != nil {
		w.conn.Close()
	}
	w.connected = false
	w.done = make(chan struct{})
	w.mu.Unlock()

	backoff := time.Second
	for {
		err := w.Connect()
		if err == nil {
			return
		}
		log.Printf("WebSocket reconnect failed: %v, retrying in %v", err, backoff)
		time.Sleep(backoff)
		if backoff < 30*time.Second {
			backoff *= 2
		}
	}
}

func (w *WSClient) Close() {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.conn != nil {
		w.conn.Close()
	}
	w.connected = false
}
