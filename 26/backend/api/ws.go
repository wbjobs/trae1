package api

import (
	"encoding/json"
	"log"
	"net/http"
	"sync"

	"greenhouse-sim/model"

	"github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin:     func(r *http.Request) bool { return true },
}

type wsManager struct {
	clients map[*websocket.Conn]struct{}
	mu      sync.RWMutex
}

func newWsManager() *wsManager {
	return &wsManager{clients: make(map[*websocket.Conn]struct{})}
}

func (m *wsManager) add(c *websocket.Conn) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.clients[c] = struct{}{}
}

func (m *wsManager) remove(c *websocket.Conn) {
	m.mu.Lock()
	defer m.mu.Unlock()
	delete(m.clients, c)
}

func (m *wsManager) broadcastSensor(r model.SensorReading) {
	msg := model.WSMessage{Type: "sensor", Data: r}
	data, err := json.Marshal(msg)
	if err != nil {
		return
	}
	m.mu.RLock()
	defer m.mu.RUnlock()
	for c := range m.clients {
		_ = c.WriteMessage(websocket.TextMessage, data)
	}
}

func (m *wsManager) broadcastAlert(a model.AlertEvent) {
	msg := model.WSMessage{Type: "alert", Data: a}
	data, err := json.Marshal(msg)
	if err != nil {
		return
	}
	m.mu.RLock()
	defer m.mu.RUnlock()
	for c := range m.clients {
		_ = c.WriteMessage(websocket.TextMessage, data)
	}
}

func (s *Server) realtimeWS(c *gin.Context) {
	conn, err := upgrader.Upgrade(c.Writer, c.Request, nil)
	if err != nil {
		log.Printf("ws upgrade err: %v", err)
		return
	}
	s.wsMgr.add(conn)
	defer func() {
		s.wsMgr.remove(conn)
		_ = conn.Close()
	}()
	for {
		if _, _, err := conn.ReadMessage(); err != nil {
			break
		}
	}
}
