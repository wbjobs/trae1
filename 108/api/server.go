package api

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"github.com/wasi-service/runtime/runtime"
	"go.uber.org/zap"
)

type Server struct {
	runtime *runtime.Runtime
	logger  *zap.Logger
	mux     *http.ServeMux
	server  *http.Server
}

func NewServer(addr string, rt *runtime.Runtime, logger *zap.Logger) *Server {
	mux := http.NewServeMux()
	s := &Server{
		runtime: rt,
		logger:  logger,
		mux:     mux,
		server: &http.Server{
			Addr:         addr,
			Handler:      mux,
			ReadTimeout:  10 * time.Second,
			WriteTimeout: 30 * time.Second,
		},
	}

	s.setupRoutes()
	return s
}

func (s *Server) setupRoutes() {
	s.mux.HandleFunc("/v1/services", s.handleServices)
	s.mux.HandleFunc("/v1/services/", s.handleServiceByName)
	s.mux.HandleFunc("/health", s.handleHealth)
}

func (s *Server) Start() error {
	s.logger.Info("starting API server", zap.String("addr", s.server.Addr))
	return s.server.ListenAndServe()
}

func (s *Server) Stop(ctx context.Context) error {
	s.logger.Info("stopping API server")
	return s.server.Shutdown(ctx)
}

func (s *Server) handleServices(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		s.listServices(w, r)
	case http.MethodPost:
		s.createService(w, r)
	default:
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
	}
}

func (s *Server) handleServiceByName(w http.ResponseWriter, r *http.Request) {
	path := r.URL.Path
	name := path[len("/v1/services/"):]

	switch r.Method {
	case http.MethodGet:
		s.getService(w, r, name)
	case http.MethodDelete:
		s.deleteService(w, r, name)
	default:
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
	}
}

func (s *Server) listServices(w http.ResponseWriter, r *http.Request) {
	services := s.runtime.ListServices()

	response := struct {
		Services []string `json:"services"`
		Count    int      `json:"count"`
	}{
		Services: services,
		Count:    len(services),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

type CreateServiceRequest struct {
	Name      string            `json:"name"`
	Module    string            `json:"module"`
	Port      int               `json:"port"`
	Instances int               `json:"instances"`
	EnvVars   map[string]string `json:"env_vars"`
}

func (s *Server) createService(w http.ResponseWriter, r *http.Request) {
	var req CreateServiceRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, fmt.Sprintf("Invalid request: %v", err), http.StatusBadRequest)
		return
	}

	ctx := context.Background()
	if err := s.runtime.StartServiceWithParams(ctx, req.Name, req.Module, req.Port, req.Instances, req.EnvVars); err != nil {
		http.Error(w, fmt.Sprintf("Failed to start service: %v", err), http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"status": "started"})
}

func (s *Server) getService(w http.ResponseWriter, r *http.Request, name string) {
	svc, exists := s.runtime.GetService(name)
	if !exists {
		http.Error(w, "Service not found", http.StatusNotFound)
		return
	}

	info := struct {
		Name     string `json:"name"`
		Running  bool   `json:"running"`
		Instances int   `json:"instances"`
		Port     int    `json:"port"`
	}{
		Name:      name,
		Running:   svc.IsRunning(),
		Instances: len(svc.GetInstances()),
		Port:      svc.GetConfig().Port,
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(info)
}

func (s *Server) deleteService(w http.ResponseWriter, r *http.Request, name string) {
	ctx := context.Background()
	if err := s.runtime.StopService(ctx, name); err != nil {
		http.Error(w, fmt.Sprintf("Failed to stop service: %v", err), http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"status": "stopped"})
}

func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.WriteHeader(http.StatusOK)
	w.Write([]byte("OK"))
}
