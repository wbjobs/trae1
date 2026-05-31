package etl

import (
	"context"
	"encoding/json"
	"fmt"
	"html/template"
	"log"
	"net/http"
	"net/http/pprof"
	"sync"
	"time"
)

type DebugServer struct {
	server   *http.Server
	loader   *PipelineLoader
	manager  *PipelineManager
	mu       sync.RWMutex
	running  bool
}

func NewDebugServer(addr string, loader *PipelineLoader, manager *PipelineManager) *DebugServer {
	mux := http.NewServeMux()

	server := &DebugServer{
		server: &http.Server{
			Addr:         addr,
			Handler:      mux,
			ReadTimeout:  10 * time.Second,
			WriteTimeout: 30 * time.Second,
		},
		loader:  loader,
		manager: manager,
	}

	server.setupRoutes(mux)

	return server
}

func (s *DebugServer) setupRoutes(mux *http.ServeMux) {
	mux.HandleFunc("/", s.handleIndex)
	mux.HandleFunc("/api/pipelines", s.handleListPipelines)
	mux.HandleFunc("/api/pipelines/", s.handlePipelineAPI)
	mux.HandleFunc("/api/debug", s.handleDebug)
	mux.HandleFunc("/api/debug/validate", s.handleValidate)
	mux.HandleFunc("/api/debug/sample", s.handleSample)
	mux.HandleFunc("/api/stats", s.handleStats)
	mux.HandleFunc("/api/reload", s.handleReload)

	mux.HandleFunc("/debug/pprof/", pprof.Index)
	mux.HandleFunc("/debug/pprof/cmdline", pprof.Cmdline)
	mux.HandleFunc("/debug/pprof/profile", pprof.Profile)
	mux.HandleFunc("/debug/pprof/symbol", pprof.Symbol)
	mux.HandleFunc("/debug/pprof/trace", pprof.Trace)
}

func (s *DebugServer) Start() error {
	s.mu.Lock()
	if s.running {
		s.mu.Unlock()
		return fmt.Errorf("server already running")
	}
	s.running = true
	s.mu.Unlock()

	go func() {
		if err := s.server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Printf("Debug server error: %v", err)
		}
	}()

	log.Printf("Debug server started on %s", s.server.Addr)
	return nil
}

func (s *DebugServer) Stop() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if !s.running {
		return nil
	}
	s.running = false

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	return s.server.Shutdown(ctx)
}

func (s *DebugServer) handleIndex(w http.ResponseWriter, r *http.Request) {
	tmpl := `<!DOCTYPE html>
<html>
<head>
    <title>ETL Pipeline Debug</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h1 { color: #333; }
        .section { margin: 20px 0; }
        .pipeline-list { display: flex; flex-wrap: wrap; gap: 10px; }
        .pipeline-item { background: #e3f2fd; padding: 10px 20px; border-radius: 4px; cursor: pointer; }
        .pipeline-item:hover { background: #bbdefb; }
        .input-area { width: 100%; min-height: 200px; font-family: monospace; padding: 10px; border: 1px solid #ddd; border-radius: 4px; }
        .output-area { width: 100%; min-height: 200px; font-family: monospace; padding: 10px; border: 1px solid #ddd; border-radius: 4px; background: #fafafa; }
        .btn { background: #2196f3; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; }
        .btn:hover { background: #1976d2; }
        .error { color: red; background: #ffebee; padding: 10px; border-radius: 4px; margin: 10px 0; }
        .success { color: green; background: #e8f5e9; padding: 10px; border-radius: 4px; margin: 10px 0; }
        .stats { display: grid; grid-template-columns: repeat(3, 1fr); gap: 20px; }
        .stat-card { background: #f5f5f5; padding: 15px; border-radius: 4px; text-align: center; }
        .stat-value { font-size: 24px; font-weight: bold; color: #1976d2; }
        .stat-label { font-size: 12px; color: #666; }
        table { width: 100%; border-collapse: collapse; }
        th, td { padding: 10px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background: #f5f5f5; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ETL Pipeline Debug Console</h1>

        <div class="section">
            <h2>Pipeline Statistics</h2>
            <div class="stats" id="stats">
                <div class="stat-card">
                    <div class="stat-value" id="total-processed">-</div>
                    <div class="stat-label">Total Processed</div>
                </div>
                <div class="stat-card">
                    <div class="stat-value" id="total-errors">-</div>
                    <div class="stat-label">Errors</div>
                </div>
                <div class="stat-card">
                    <div class="stat-value" id="avg-latency">-</div>
                    <div class="stat-label">Avg Latency (μs)</div>
                </div>
            </div>
        </div>

        <div class="section">
            <h2>Available Pipelines</h2>
            <div class="pipeline-list" id="pipelines"></div>
        </div>

        <div class="section">
            <h2>Debug Test</h2>
            <form id="debug-form">
                <label>Pipeline:</label>
                <select id="pipeline-select" style="width: 100%; padding: 10px; margin: 10px 0;">
                </select>

                <label>Input BSON (JSON):</label>
                <textarea id="input-doc" class="input-area" placeholder='{"name": "John", "age": 30, "birth_year": 1994}'></textarea>

                <button type="submit" class="btn">Execute</button>
            </form>
        </div>

        <div class="section">
            <h2>Output</h2>
            <div id="output" class="output-area"></div>
            <div id="latency"></div>
        </div>

        <div class="section">
            <h2>Recent DLQ Messages</h2>
            <button onclick="loadDLQ()" class="btn">Refresh</button>
            <div id="dlq-messages"></div>
        </div>
    </div>

    <script>
        let currentPipeline = '';

        async function loadPipelines() {
            const res = await fetch('/api/pipelines');
            const data = await res.json();
            const select = document.getElementById('pipeline-select');
            const list = document.getElementById('pipelines');
            select.innerHTML = '';
            list.innerHTML = '';

            for (const name of data.pipelines) {
                select.innerHTML += '<option value="' + name + '">' + name + '</option>';
                list.innerHTML += '<div class="pipeline-item" onclick="selectPipeline(\'' + name + '\')">' + name + '</div>';
            }

            if (data.pipelines.length > 0) {
                selectPipeline(data.pipelines[0]);
            }
        }

        function selectPipeline(name) {
            currentPipeline = name;
            document.getElementById('pipeline-select').value = name;
            loadSample(name);
        }

        async function loadSample(name) {
            const res = await fetch('/api/debug/sample?pipeline=' + name);
            const data = await res.json();
            if (data.sample) {
                document.getElementById('input-doc').value = JSON.stringify(data.sample, null, 2);
            }
        }

        async function loadStats() {
            const res = await fetch('/api/stats');
            const data = await res.json();

            for (const [name, stats] of Object.entries(data)) {
                document.getElementById('total-processed').textContent = stats.processed_count || 0;
                document.getElementById('total-errors').textContent = stats.error_count || 0;
                document.getElementById('avg-latency').textContent = (stats.avg_latency_micros || 0).toFixed(2);
            }
        }

        document.getElementById('debug-form').onsubmit = async (e) => {
            e.preventDefault();

            const input = document.getElementById('input-doc').value;
            let inputDoc;
            try {
                inputDoc = JSON.parse(input);
            } catch (err) {
                document.getElementById('output').innerHTML = '<div class="error">Invalid JSON: ' + err.message + '</div>';
                return;
            }

            const res = await fetch('/api/debug', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    pipeline_name: currentPipeline,
                    input_doc: inputDoc
                })
            });

            const data = await res.json();
            const outputDiv = document.getElementById('output');

            if (data.errors && data.errors.length > 0) {
                outputDiv.innerHTML = '<div class="error">Errors: ' + data.errors.join(', ') + '</div>';
            } else if (data.filtered) {
                outputDiv.innerHTML = '<div class="success">Document was filtered out</div>';
            } else {
                outputDiv.innerHTML = '<pre>' + JSON.stringify(data.output_doc, null, 2) + '</pre>';
            }

            document.getElementById('latency').textContent = 'Latency: ' + data.latency_us + ' μs (< 1ms requirement: ' + (data.latency_us < 1000 ? '✓' : '✗') + ')';
            loadStats();
        };

        async function loadDLQ() {
            // Placeholder for DLQ fetch
            document.getElementById('dlq-messages').innerHTML = '<p>DLQ functionality requires Kafka integration</p>';
        }

        loadPipelines();
        loadStats();
        setInterval(loadStats, 5000);
    </script>
</body>
</html>`

	t, err := template.New("index").Parse(tmpl)
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}

	w.Header().Set("Content-Type", "text/html")
	t.Execute(w, nil)
}

func (s *DebugServer) handleListPipelines(w http.ResponseWriter, r *http.Request) {
	pipelines := s.loader.ListPipelines()

	json.NewEncoder(w).Encode(map[string]interface{}{
		"pipelines": pipelines,
	})
}

func (s *DebugServer) handlePipelineAPI(w http.ResponseWriter, r *http.Request) {
	path := r.URL.Path[len("/api/pipelines/"):]

	switch r.Method {
	case http.MethodGet:
		pipeline, err := s.loader.GetPipeline(path)
		if err != nil {
			http.Error(w, err.Error(), 404)
			return
		}

		stages := make([]string, 0)
		for _, stage := range pipeline.GetStages() {
			stages = append(stages, stage.Name())
		}

		json.NewEncoder(w).Encode(map[string]interface{}{
			"name":   path,
			"stages": stages,
		})

	case http.MethodPost:
		var req struct {
			Action string `json:"action"`
		}
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, err.Error(), 400)
			return
		}

		if req.Action == "reload" {
			if err := s.loader.LoadFromFile(path); err != nil {
				http.Error(w, err.Error(), 500)
				return
			}
			w.WriteHeader(200)
		}

	case http.MethodDelete:
		if err := s.manager.Unregister(path); err != nil {
			http.Error(w, err.Error(), 500)
			return
		}
		w.WriteHeader(204)
	}
}

func (s *DebugServer) handleDebug(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", 405)
		return
	}

	var req PipelineDebugRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), 400)
		return
	}

	response, err := s.loader.DebugExecute(r.Context(), req)
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}

	json.NewEncoder(w).Encode(response)
}

func (s *DebugServer) handleValidate(w http.ResponseWriter, r *http.Request) {
	name := r.URL.Query().Get("name")
	if name == "" {
		http.Error(w, "name required", 400)
		return
	}

	valid, errors, err := s.loader.ValidatePipeline(name)
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}

	json.NewEncoder(w).Encode(map[string]interface{}{
		"valid":  valid,
		"errors": errors,
	})
}

func (s *DebugServer) handleSample(w http.ResponseWriter, r *http.Request) {
	name := r.URL.Query().Get("pipeline")
	if name == "" {
		http.Error(w, "pipeline required", 400)
		return
	}

	sample := map[string]interface{}{
		"name":       "John Doe",
		"age":        30,
		"birth_year": 1994,
		"email":      "john@example.com",
		"address": map[string]interface{}{
			"city":    "New York",
			"zip":     "10001",
			"country": "USA",
		},
		"tags": []string{"premium", "active"},
	}

	json.NewEncoder(w).Encode(map[string]interface{}{
		"sample": sample,
	})
}

func (s *DebugServer) handleStats(w http.ResponseWriter, r *http.Request) {
	stats := make(map[string]interface{})

	for _, name := range s.loader.ListPipelines() {
		stat, err := s.manager.GetStats(name)
		if err != nil {
			continue
		}
		stats[name] = stat
	}

	json.NewEncoder(w).Encode(stats)
}

func (s *DebugServer) handleReload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", 405)
		return
	}

	var req struct {
		Path string `json:"path"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), 400)
		return
	}

	if err := s.loader.LoadFromFile(req.Path); err != nil {
		http.Error(w, err.Error(), 500)
		return
	}

	json.NewEncoder(w).Encode(map[string]interface{}{
		"success": true,
		"message": "Configuration reloaded",
	})
}
