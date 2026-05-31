package main

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"log"
	"math/big"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/quic-go/webtransport-go"
)

const (
	NumSensors       = 100
	DataInterval     = 10 * time.Millisecond
	RecordDuration   = 5 * time.Minute
	MaxStreams       = 100
	ListenAddr       = ":4433"
	CertDir          = "certs"
)

type SensorData struct {
	SensorID  int       `json:"sensor_id"`
	Timestamp int64     `json:"timestamp"`
	Temp      float64   `json:"temp"`
	Pressure  float64   `json:"pressure"`
	Flow      float64   `json:"flow"`
	Vibration float64   `json:"vibration"`
	Seq       uint64    `json:"seq"`
}

type ClientSession struct {
	mu          sync.Mutex
	session     *webtransport.Session
	streams     map[int]webtransport.SendStream
	subscribed  map[int]bool
	lastSeq     map[int]uint64
}

type RecordManager struct {
	mu       sync.RWMutex
	buffer   []SensorData
	capacity int
	index    int
	full     bool
}

type Server struct {
	wtServer    *webtransport.Server
	sessions    sync.Map
	recordMgr   *RecordManager
	sensors     []*SimulatedSensor
}

type SimulatedSensor struct {
	ID        int
	baseTemp  float64
	basePress float64
	baseFlow  float64
	baseVib   float64
	seq       uint64
}

func NewSimulatedSensor(id int) *SimulatedSensor {
	return &SimulatedSensor{
		ID:        id,
		baseTemp:  20 + float64(id%10)*5,
		basePress: 100 + float64(id%20)*10,
		baseFlow:  50 + float64(id%15)*8,
		baseVib:   0.1 + float64(id%5)*0.05,
	}
}

func (s *SimulatedSensor) Generate(timestamp int64) SensorData {
	s.seq++
	noise := func(base float64, amplitude float64) float64 {
		b := make([]byte, 8)
		rand.Read(b)
		n := float64(int64(binary.BigEndian.Uint64(b))%1000) / 1000.0
		return base + (n-0.5)*amplitude
	}
	return SensorData{
		SensorID:  s.ID,
		Timestamp: timestamp,
		Temp:      noise(s.baseTemp, 2.0),
		Pressure:  noise(s.basePress, 5.0),
		Flow:      noise(s.baseFlow, 3.0),
		Vibration: noise(s.baseVib, 0.1),
		Seq:       s.seq,
	}
}

func NewRecordManager(duration time.Duration, interval time.Duration) *RecordManager {
	capacity := int(duration / interval) * NumSensors
	return &RecordManager{
		buffer:   make([]SensorData, capacity),
		capacity: capacity,
	}
}

func (r *RecordManager) Add(data SensorData) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.buffer[r.index] = data
	r.index = (r.index + 1) % r.capacity
	if r.index == 0 {
		r.full = true
	}
}

func (r *RecordManager) GetAll() []SensorData {
	r.mu.RLock()
	defer r.mu.RUnlock()
	if !r.full {
		result := make([]SensorData, r.index)
		copy(result, r.buffer[:r.index])
		return result
	}
	result := make([]SensorData, r.capacity)
	copy(result, r.buffer[r.index:])
	copy(result[r.capacity-r.index:], r.buffer[:r.index])
	return result
}

func (r *RecordManager) GetBySensor(sensorID int) []SensorData {
	r.mu.RLock()
	defer r.mu.RUnlock()
	var result []SensorData
	if !r.full {
		for i := 0; i < r.index; i++ {
			if r.buffer[i].SensorID == sensorID {
				result = append(result, r.buffer[i])
			}
		}
	} else {
		for i := 0; i < r.capacity; i++ {
			idx := (r.index + i) % r.capacity
			if r.buffer[idx].SensorID == sensorID {
				result = append(result, r.buffer[idx])
			}
		}
	}
	return result
}

func generateCerts() error {
	if _, err := os.Stat(filepath.Join(CertDir, "cert.pem")); err == nil {
		return nil
	}
	if err := os.MkdirAll(CertDir, 0755); err != nil {
		return err
	}
	priv, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return err
	}
	template := x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject: pkix.Name{
			Organization: []string{"Industrial Sensor Monitor"},
		},
		NotBefore:             time.Now(),
		NotAfter:              time.Now().Add(365 * 24 * time.Hour),
		KeyUsage:              x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
	}
	derBytes, err := x509.CreateCertificate(rand.Reader, &template, &template, &priv.PublicKey, priv)
	if err != nil {
		return err
	}
	certPEM := pemEncode(derBytes, "CERTIFICATE")
	keyBytes, err := x509.MarshalECPrivateKey(priv)
	if err != nil {
		return err
	}
	keyPEM := pemEncode(keyBytes, "EC PRIVATE KEY")
	if err := os.WriteFile(filepath.Join(CertDir, "cert.pem"), []byte(certPEM), 0644); err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(CertDir, "key.pem"), []byte(keyPEM), 0644); err != nil {
		return err
	}
	return nil
}

func pemEncode(derBytes []byte, blockType string) string {
	return fmt.Sprintf("-----BEGIN %s-----\n%s\n-----END %s-----\n",
		blockType,
		base64Encode(derBytes),
		blockType)
}

func base64Encode(data []byte) string {
	const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
	result := make([]byte, (len(data)+2)/3*4)
	for i := 0; i < len(data); i += 3 {
		var b [3]byte
		copy(b[:], data[i:])
		result[i*4/3] = alphabet[b[0]>>2]
		result[i*4/3+1] = alphabet[(b[0]&3)<<4|b[1]>>4]
		if i+1 < len(data) {
			result[i*4/3+2] = alphabet[(b[1]&15)<<2|b[2]>>6]
		} else {
			result[i*4/3+2] = '='
		}
		if i+2 < len(data) {
			result[i*4/3+3] = alphabet[b[2]&63]
		} else {
			result[i*4/3+3] = '='
		}
	}
	return string(result)
}

func NewServer() (*Server, error) {
	if err := generateCerts(); err != nil {
		return nil, fmt.Errorf("generate certs: %w", err)
	}
	cert, err := tls.LoadX509KeyPair(
		filepath.Join(CertDir, "cert.pem"),
		filepath.Join(CertDir, "key.pem"),
	)
	if err != nil {
		return nil, fmt.Errorf("load cert: %w", err)
	}
	tlsConfig := &tls.Config{
		Certificates: []tls.Certificate{cert},
		NextProtos:   []string{"webtransport"},
	}
	wtServer := &webtransport.Server{
		H3: webtransport.H3Config{
			TLSConfig: tlsConfig,
			QUICConfig: &webtransport.QUICConfig{
				MaxIncomingStreams:    MaxStreams,
				MaxIncomingUniStreams: MaxStreams,
			},
		},
		CheckOrigin: func(r *http.Request) bool {
			return true
		},
	}
	sensors := make([]*SimulatedSensor, NumSensors)
	for i := 0; i < NumSensors; i++ {
		sensors[i] = NewSimulatedSensor(i)
	}
	return &Server{
		wtServer:  wtServer,
		recordMgr: NewRecordManager(RecordDuration, DataInterval),
		sensors:   sensors,
	}, nil
}

func (s *Server) handleSession(ctx context.Context, session *webtransport.Session) {
	log.Printf("New WebTransport session established")
	cs := &ClientSession{
		session:    session,
		streams:    make(map[int]webtransport.SendStream),
		subscribed: make(map[int]bool),
		lastSeq:    make(map[int]uint64),
	}
	s.sessions.Store(session, cs)
	defer func() {
		s.sessions.Delete(session)
		session.CloseWithError(0, "session ended")
	}()
	go s.handleIncomingStreams(cs)
	<-ctx.Done()
}

func (s *Server) handleIncomingStreams(cs *ClientSession) {
	for {
		stream, err := cs.session.AcceptStream(context.Background())
		if err != nil {
			return
		}
		go s.handleControlStream(cs, stream)
	}
}

type ControlMessage struct {
	Type     string `json:"type"`
	SensorID int    `json:"sensor_id"`
}

func (s *Server) handleControlStream(cs *ClientSession, stream webtransport.ReceiveStream) {
	defer stream.Close()
	buf := make([]byte, 4096)
	n, err := stream.Read(buf)
	if err != nil {
		return
	}
	var msg ControlMessage
	if err := json.Unmarshal(buf[:n], &msg); err != nil {
		log.Printf("Error parsing control message: %v", err)
		return
	}
	cs.mu.Lock()
	defer cs.mu.Unlock()
	switch msg.Type {
	case "subscribe":
		if msg.SensorID >= 0 && msg.SensorID < NumSensors {
			cs.subscribed[msg.SensorID] = true
			sendStream, err := cs.session.OpenStream()
			if err != nil {
				log.Printf("Error opening stream: %v", err)
				return
			}
			cs.streams[msg.SensorID] = sendStream
			log.Printf("Client subscribed to sensor %d", msg.SensorID)
		}
	case "unsubscribe":
		if st, ok := cs.streams[msg.SensorID]; ok {
			st.Close()
			delete(cs.streams, msg.SensorID)
		}
		delete(cs.subscribed, msg.SensorID)
	case "replay":
		history := s.recordMgr.GetBySensor(msg.SensorID)
		sendStream, err := cs.session.OpenStream()
		if err != nil {
			return
		}
		defer sendStream.Close()
		for _, data := range history {
			dataBytes, _ := json.Marshal(data)
			length := make([]byte, 4)
			binary.BigEndian.PutUint32(length, uint32(len(dataBytes)))
			if _, err := sendStream.Write(length); err != nil {
				return
			}
			if _, err := sendStream.Write(dataBytes); err != nil {
				return
			}
		}
	case "replay_all":
		history := s.recordMgr.GetAll()
		sendStream, err := cs.session.OpenStream()
		if err != nil {
			return
		}
		defer sendStream.Close()
		for _, data := range history {
			dataBytes, _ := json.Marshal(data)
			length := make([]byte, 4)
			binary.BigEndian.PutUint32(length, uint32(len(dataBytes)))
			if _, err := sendStream.Write(length); err != nil {
				return
			}
			if _, err := sendStream.Write(dataBytes); err != nil {
				return
			}
		}
	}
}

func (s *Server) collectAndDistribute() {
	ticker := time.NewTicker(DataInterval)
	defer ticker.Stop()
	for range ticker.C {
		now := time.Now().UnixNano() / int64(time.Millisecond)
		for _, sensor := range s.sensors {
			data := sensor.Generate(now)
			s.recordMgr.Add(data)
			s.sessions.Range(func(key, value interface{}) bool {
				cs := value.(*ClientSession)
				cs.mu.Lock()
				defer cs.mu.Unlock()
				if cs.subscribed[data.SensorID] {
					if stream, ok := cs.streams[data.SensorID]; ok {
						dataBytes, _ := json.Marshal(data)
						length := make([]byte, 4)
						binary.BigEndian.PutUint32(length, uint32(len(dataBytes)))
						if _, err := stream.Write(length); err == nil {
							if _, err := stream.Write(dataBytes); err != nil {
								log.Printf("Error writing to stream for sensor %d: %v", data.SensorID, err)
								stream.Close()
								delete(cs.streams, data.SensorID)
							}
						}
					}
				}
				return true
			})
		}
	}
}

func (s *Server) Start() error {
	go s.collectAndDistribute()
	mux := http.NewServeMux()
	mux.HandleFunc("/webtransport", func(w http.ResponseWriter, r *http.Request) {
		session, err := s.wtServer.Upgrade(w, r)
		if err != nil {
			log.Printf("Error upgrading to WebTransport: %v", err)
			return
		}
		s.handleSession(r.Context(), session)
	})
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{"status": "ok"})
	})
	log.Printf("Server starting on %s", ListenAddr)
	return s.wtServer.ListenAndServe(ListenAddr, mux)
}

func main() {
	server, err := NewServer()
	if err != nil {
		log.Fatalf("Failed to create server: %v", err)
	}
	if err := server.Start(); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}
