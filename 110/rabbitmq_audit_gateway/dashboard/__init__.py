"""Web Dashboard for RabbitMQ Audit Gateway"""
import time
import logging
from typing import Dict, Any, List, Optional
from threading import Thread
from collections import deque

try:
    from flask import Flask, jsonify, render_template, request, Response
    from flask_cors import CORS
    FLASK_AVAILABLE = True
except ImportError:
    FLASK_AVAILABLE = False

logger = logging.getLogger(__name__)


class DashboardData:
    def __init__(self, max_points: int = 60):
        self.max_points = max_points
        self._message_flow: deque = deque(maxlen=max_points)
        self._interception_stats: deque = deque(maxlen=max_points)
        self._latency_data: deque = deque(maxlen=max_points)
        self._start_time = time.time()

    def add_flow_data(self, published: int, consumed: int, intercepted: int) -> None:
        self._message_flow.append({
            "timestamp": time.time(),
            "published": published,
            "consumed": consumed,
            "intercepted": intercepted
        })

    def add_interception_data(self, rule_stats: Dict[str, int]) -> None:
        self._interception_stats.append({
            "timestamp": time.time(),
            "rules": dict(rule_stats)
        })

    def add_latency_data(self, avg_ms: float, p99_ms: float) -> None:
        self._latency_data.append({
            "timestamp": time.time(),
            "avg": avg_ms,
            "p99": p99_ms
        })

    def get_flow_trend(self) -> List[Dict[str, Any]]:
        return list(self._message_flow)

    def get_interception_trend(self) -> List[Dict[str, Any]]:
        return list(self._interception_stats)

    def get_latency_trend(self) -> List[Dict[str, Any]]:
        return list(self._latency_data)

    def get_uptime(self) -> float:
        return time.time() - self._start_time


class DashboardServer:
    def __init__(self, config, gateway):
        if not FLASK_AVAILABLE:
            raise ImportError("flask and flask-cors are not installed. Install with: pip install flask flask-cors")

        self.config = config
        self.gateway = gateway
        self.app = Flask(__name__, template_folder='templates', static_folder='static')
        self._data = DashboardData()
        self._running = False
        self._thread: Optional[Thread] = None
        self._last_published = 0
        self._last_consumed = 0
        self._last_intercepted = 0

        CORS(self.app)
        self._setup_routes()

    def _setup_routes(self) -> None:
        @self.app.route('/')
        def index():
            return render_template('index.html')

        @self.app.route('/api/stats')
        def stats():
            gateway_stats = self.gateway.get_stats()
            return jsonify(gateway_stats)

        @self.app.route('/api/stats/flow')
        def flow_stats():
            return jsonify({
                "trend": self._data.get_flow_trend(),
                "current": {
                    "published": self._get_current_published(),
                    "consumed": self._get_current_consumed(),
                    "intercepted": self._get_current_intercepted()
                }
            })

        @self.app.route('/api/stats/interceptions')
        def interception_stats():
            return jsonify({
                "trend": self._data.get_interception_trend()
            })

        @self.app.route('/api/stats/latency')
        def latency_stats():
            return jsonify({
                "trend": self._data.get_latency_trend()
            })

        @self.app.route('/api/rules')
        def rules():
            return jsonify({
                "rules": self.gateway.get_interception_rules()
            })

        @self.app.route('/api/rules/<rule_id>/enable', methods=['POST'])
        def enable_rule(rule_id):
            success = self.gateway._interception_engine.enable_rule(rule_id)
            return jsonify({"success": success, "rule_id": rule_id})

        @self.app.route('/api/rules/<rule_id>/disable', methods=['POST'])
        def disable_rule(rule_id):
            success = self.gateway._interception_engine.disable_rule(rule_id)
            return jsonify({"success": success, "rule_id": rule_id})

        @self.app.route('/api/rules/reload', methods=['POST'])
        def reload_rules():
            self.gateway.reload_interception_rules()
            return jsonify({"success": True})

        @self.app.route('/api/reset', methods=['POST'])
        def reset_stats():
            self.gateway.reset_stats()
            return jsonify({"success": True})

        @self.app.route('/api/health')
        def health():
            return jsonify({
                "status": "healthy",
                "uptime": self._data.get_uptime()
            })

        @self.app.route('/api/ha/status')
        def ha_status():
            stats = self.gateway.get_stats()
            return jsonify(stats.get("ha", {}))

        @self.app.route('/api/sampling/config')
        def sampling_config():
            return jsonify(self.gateway._sampler.get_stats())

        @self.app.route('/api/sampling/rate', methods=['POST'])
        def set_sampling_rate():
            data = request.get_json()
            rate = data.get('rate', 0.01)
            self.gateway._sampler.set_rate(rate)
            return jsonify({"success": True, "rate": rate})

    def _get_current_published(self) -> int:
        stats = self.gateway.get_stats()
        return stats.get("audit", {}).get("total_published", 0)

    def _get_current_consumed(self) -> int:
        stats = self.gateway.get_stats()
        return stats.get("audit", {}).get("total_consumed", 0)

    def _get_current_intercepted(self) -> int:
        stats = self.gateway.get_stats()
        return stats.get("audit", {}).get("total_intercepted", 0)

    def _update_data(self) -> None:
        current_published = self._get_current_published()
        current_consumed = self._get_current_consumed()
        current_intercepted = self._get_current_intercepted()

        new_published = current_published - self._last_published
        new_consumed = current_consumed - self._last_consumed
        new_intercepted = current_intercepted - self._last_intercepted

        self._data.add_flow_data(new_published, new_consumed, new_intercepted)

        self._last_published = current_published
        self._last_consumed = current_consumed
        self._last_intercepted = current_intercepted

        stats = self.gateway.get_stats()
        perf_stats = stats.get("performance", {})
        if perf_stats:
            avg_latency = perf_stats.get("message.publish", {}).get("avg_ms", 0)
            p99_latency = perf_stats.get("message.publish", {}).get("p99_ms", 0)
            self._data.add_latency_data(avg_latency, p99_latency)

        interceptions = stats.get("audit", {}).get("interceptions_by_rule", {})
        if interceptions:
            self._data.add_interception_data(interceptions)

    def _update_loop(self) -> None:
        while self._running:
            try:
                self._update_data()
            except Exception as e:
                logger.error(f"Error updating dashboard data: {e}")
            time.sleep(self.config.dashboard.refresh_interval)

    def start(self) -> None:
        self._running = True
        self._thread = Thread(target=self._update_loop, daemon=True)
        self._thread.start()

        logger.info(f"Dashboard server starting on {self.config.dashboard.host}:{self.config.dashboard.port}")

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=5)
            self._thread = None

    def run(self) -> None:
        self.app.run(
            host=self.config.dashboard.host,
            port=self.config.dashboard.port,
            debug=False,
            use_reloader=False
        )
