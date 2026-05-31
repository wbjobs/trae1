package http

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net/http"
	"sync"
	"time"

	"github.com/wasi-service/runtime/wasi"
	"go.uber.org/zap"
)

type Handler struct {
	instanceManager *wasi.InstanceManager
	logger          *zap.Logger
	instanceID      string
	instance        *wasi.WasmInstance
	serviceName     string
	mu              sync.RWMutex
}

func NewHandler(instanceManager *wasi.InstanceManager, serviceName string, logger *zap.Logger) *Handler {
	return &Handler{
		instanceManager: instanceManager,
		serviceName:     serviceName,
		logger:          logger,
	}
}

func (h *Handler) SetInstance(instance *wasi.WasmInstance) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.instance = instance
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	h.mu.RLock()
	instance := h.instance
	h.mu.RUnlock()

	if instance == nil {
		h.logger.Error("no wasm instance available")
		http.Error(w, "Service Unavailable", http.StatusServiceUnavailable)
		return
	}

	method := []byte(r.Method)
	path := []byte(r.URL.Path)
	if r.URL.RawQuery != "" {
		path = append(path, []byte("?"+r.URL.RawQuery)...)
	}

	var body []byte
	if r.Body != nil {
		body, _ = io.ReadAll(r.Body)
	}

	headers := make(map[string]string)
	for k, v := range r.Header {
		if len(v) > 0 {
			headers[k] = v[0]
		}
	}

	result := instance.HandleRequest(r.Context(), method, path, body, headers)

	if result.Error != nil {
		if result.StatusCode == http.StatusGatewayTimeout {
			h.logger.Warn("request timed out", zap.Error(result.Error))
			http.Error(w, "Gateway Timeout", http.StatusGatewayTimeout)
		} else {
			h.logger.Error("failed to handle request", zap.Error(result.Error))
			http.Error(w, "Internal Server Error", http.StatusInternalServerError)
		}
		return
	}

	for k, v := range result.Headers {
		w.Header().Set(k, v)
	}
	w.WriteHeader(result.StatusCode)

	if len(result.Body) > 0 {
		w.Write(result.Body)
	}
}

type HTTPServer struct {
	handler *Handler
	server  *http.Server
	logger  *zap.Logger
}

func NewHTTPServer(addr string, handler *Handler, logger *zap.Logger) *HTTPServer {
	return &HTTPServer{
		handler: handler,
		server: &http.Server{
			Addr:         addr,
			Handler:      handler,
			ReadTimeout:  30 * time.Second,
			WriteTimeout: 30 * time.Second,
			IdleTimeout:  120 * time.Second,
		},
		logger: logger,
	}
}

func (s *HTTPServer) Start() error {
	s.logger.Info("starting HTTP server", zap.String("addr", s.server.Addr))
	return s.server.ListenAndServe()
}

func (s *HTTPServer) Stop(ctx context.Context) error {
	s.logger.Info("stopping HTTP server")
	return s.server.Shutdown(ctx)
}

type LoadBalancer struct {
	instances    []*wasi.WasmInstance
	currentIndex uint64
	mu           sync.RWMutex
	logger       *zap.Logger
}

func NewLoadBalancer(logger *zap.Logger) *LoadBalancer {
	return &LoadBalancer{
		logger: logger,
	}
}

func (lb *LoadBalancer) AddInstance(instance *wasi.WasmInstance) {
	lb.mu.Lock()
	defer lb.mu.Unlock()
	lb.instances = append(lb.instances, instance)
}

func (lb *LoadBalancer) RemoveInstance(instanceID string) {
	lb.mu.Lock()
	defer lb.mu.Unlock()
	newInstances := make([]*wasi.WasmInstance, 0, len(lb.instances))
	for _, inst := range lb.instances {
		if inst.ID() != instanceID {
			newInstances = append(newInstances, inst)
		}
	}
	lb.instances = newInstances
}

func (lb *LoadBalancer) GetNextInstance() (*wasi.WasmInstance, error) {
	lb.mu.RLock()
	defer lb.mu.RUnlock()

	if len(lb.instances) == 0 {
		return nil, fmt.Errorf("no instances available")
	}

	idx := lb.currentIndex % uint64(len(lb.instances))
	lb.currentIndex++
	return lb.instances[idx], nil
}

func (lb *LoadBalancer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	instance, err := lb.GetNextInstance()
	if err != nil {
		lb.logger.Error("no instances available")
		http.Error(w, "Service Unavailable", http.StatusServiceUnavailable)
		return
	}

	method := []byte(r.Method)
	path := []byte(r.URL.Path)
	if r.URL.RawQuery != "" {
		path = append(path, []byte("?"+r.URL.RawQuery)...)
	}

	var body []byte
	if r.Body != nil {
		body, _ = io.ReadAll(r.Body)
	}

	headers := make(map[string]string)
	for k, v := range r.Header {
		if len(v) > 0 {
			headers[k] = v[0]
		}
	}

	result := instance.HandleRequest(r.Context(), method, path, body, headers)

	if result.Error != nil {
		if result.StatusCode == http.StatusGatewayTimeout {
			lb.logger.Warn("request timed out", zap.Error(result.Error))
			http.Error(w, "Gateway Timeout", http.StatusGatewayTimeout)
		} else {
			lb.logger.Error("failed to handle request", zap.Error(result.Error))
			http.Error(w, "Internal Server Error", http.StatusInternalServerError)
		}
		return
	}

	for k, v := range result.Headers {
		w.Header().Set(k, v)
	}
	w.WriteHeader(result.StatusCode)

	if len(result.Body) > 0 {
		w.Write(result.Body)
	}
}

type Response struct {
	StatusCode int
	Headers    map[string]string
	Body       []byte
}

func SerializeResponse(resp *Response) []byte {
	var buf bytes.Buffer

	buf.WriteByte(byte(resp.StatusCode >> 8))
	buf.WriteByte(byte(resp.StatusCode & 0xFF))

	headerCount := len(resp.Headers)
	buf.WriteByte(byte(headerCount >> 8))
	buf.WriteByte(byte(headerCount & 0xFF))

	bodyLen := len(resp.Body)
	buf.WriteByte(byte(bodyLen >> 24))
	buf.WriteByte(byte((bodyLen >> 16) & 0xFF))
	buf.WriteByte(byte((bodyLen >> 8) & 0xFF))
	buf.WriteByte(byte(bodyLen & 0xFF))

	for k, v := range resp.Headers {
		kLen := len(k)
		vLen := len(v)
		buf.WriteByte(byte(kLen >> 8))
		buf.WriteByte(byte(kLen & 0xFF))
		buf.Write([]byte(k))
		buf.WriteByte(byte(vLen >> 8))
		buf.WriteByte(byte(vLen & 0xFF))
		buf.Write([]byte(v))
	}

	buf.Write(resp.Body)
	return buf.Bytes()
}