package main

import (
	"encoding/json"
	"log"
	"math/rand"
	"net/http"
	"time"

	"github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

type MockServer struct {
	addr          string
	stocks        []string
	prices        map[string]float64
	ticksPerStock int
}

func NewMockServer(addr string, numStocks int, ticksPerStock int) *MockServer {
	stocks := make([]string, numStocks)
	prices := make(map[string]float64, numStocks)
	for i := 0; i < numStocks; i++ {
		code := generateStockCode(i)
		stocks[i] = code
		prices[code] = 10.0 + rand.Float64()*90.0
	}
	return &MockServer{
		addr:          addr,
		stocks:        stocks,
		prices:        prices,
		ticksPerStock: ticksPerStock,
	}
}

func generateStockCode(index int) string {
	prefixes := []string{"000", "001", "002", "300", "600", "601", "603", "688"}
	prefix := prefixes[index%len(prefixes)]
	suffix := (index % 1000)
	return prefix + intToStr(suffix, 3)
}

func intToStr(n, width int) string {
	s := ""
	for i := 0; i < width; i++ {
		d := n % 10
		s = string(rune('0'+d)) + s
		n /= 10
	}
	return s
}

func (m *MockServer) Start() {
	http.HandleFunc("/ticks", m.handleTicks)
	log.Printf("Mock WebSocket server starting on %s (stocks: %d, ticks/stock: %d)",
		m.addr, len(m.stocks), m.ticksPerStock)
	go func() {
		if err := http.ListenAndServe(m.addr, nil); err != nil {
			log.Fatalf("Mock server error: %v", err)
		}
	}()
}

func (m *MockServer) handleTicks(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("Mock upgrade error: %v", err)
		return
	}
	defer conn.Close()

	log.Printf("Mock client connected, sending ticks...")
	ticker := time.NewTicker(500 * time.Millisecond)
	defer ticker.Stop()

	for range ticker.C {
		now := time.Now().UnixMilli()

		for _, code := range m.stocks {
			for t := 0; t < m.ticksPerStock; t++ {
				prevPrice := m.prices[code]
				change := (rand.Float64() - 0.5) * 0.4
				newPrice := prevPrice + change
				if newPrice < 1.0 {
					newPrice = 1.0
				}
				m.prices[code] = newPrice

				tick := Tick{
					Code:      code,
					Price:     newPrice,
					Timestamp: now,
				}

				data, err := json.Marshal(tick)
				if err != nil {
					continue
				}

				if err := conn.WriteMessage(websocket.TextMessage, data); err != nil {
					log.Printf("Mock write error: %v", err)
					return
				}
			}
		}
	}
}
