"""
REST API Server for DDoS Defense Gateway v3
Provides endpoints for managing defense policies, hierarchical meters, ML-driven thresholds
"""

import json
import logging
from typing import Dict, Optional

from flask import Flask, request, jsonify, Response
from flask_cors import CORS

from .p4_controller import P4Controller, ProtocolType, DropReason, CounterType
from .metrics import MetricsCollector
from .auto_blocker import AutoBlocker
from .ml_model import ThresholdType, ModelMode

logger = logging.getLogger(__name__)


class DefenseAPI:
    """REST API for DDoS Defense Gateway with ML adaptive threshold support"""

    def __init__(self, p4_controller: P4Controller,
                 metrics_collector: MetricsCollector,
                 auto_blocker: AutoBlocker,
                 adaptive_manager=None,
                 traffic_collector=None,
                 ml_model=None,
                 host: str = "0.0.0.0",
                 port: int = 8080):
        self.app = Flask(__name__)
        CORS(self.app)

        self.p4_controller = p4_controller
        self.metrics = metrics_collector
        self.auto_blocker = auto_blocker
        self.adaptive_manager = adaptive_manager
        self.traffic_collector = traffic_collector
        self.ml_model = ml_model
        self.host = host
        self.port = port

        self._register_routes()

    def _register_routes(self):
        """Register all API routes"""

        @self.app.route('/api/health', methods=['GET'])
        def health_check():
            status = {
                'status': 'healthy',
                'controller_connected': self.p4_controller.is_connected(),
                'auto_blocker_running': self.auto_blocker._running
            }
            if self.adaptive_manager:
                status['adaptive_manager_running'] = self.adaptive_manager._running
            if self.ml_model:
                status['ml_model_trained'] = self.ml_model.is_trained()
            return jsonify(status)

        @self.app.route('/api/blacklist', methods=['GET'])
        def get_blacklist():
            """Get all blacklisted IPs"""
            blacklist = self.p4_controller.get_blacklist_ips()
            return jsonify({
                'count': len(blacklist),
                'entries': blacklist
            })

        @self.app.route('/api/blacklist', methods=['POST'])
        def add_blacklist():
            """Add IP to blacklist"""
            data = request.get_json()
            if not data or 'ip' not in data:
                return jsonify({'error': 'IP address required'}), 400

            ip = data['ip']
            duration = data.get('duration', 300)

            success = self.p4_controller.add_blacklist_ip(ip, duration)
            if success:
                self.metrics.record_defense_action('manual_blacklist')
                self.metrics.update_blacklist_count(
                    len(self.p4_controller.get_blacklist_ips())
                )
                return jsonify({'status': 'success', 'ip': ip, 'duration': duration})
            return jsonify({'status': 'error', 'message': 'Failed to add blacklist'}), 500

        @self.app.route('/api/blacklist/<ip>', methods=['DELETE'])
        def remove_blacklist(ip):
            """Remove IP from blacklist"""
            success = self.p4_controller.remove_blacklist_ip(ip)
            if success:
                self.metrics.update_blacklist_count(
                    len(self.p4_controller.get_blacklist_ips())
                )
                return jsonify({'status': 'success', 'ip': ip})
            return jsonify({'status': 'error', 'message': 'Failed to remove'}), 500

        @self.app.route('/api/icmp/blocked', methods=['GET'])
        def get_icmp_blocked():
            """Get ICMP blocked IPs"""
            blocked = self.p4_controller.get_icmp_blocked_ips()
            return jsonify({
                'count': len(blocked),
                'entries': blocked
            })

        @self.app.route('/api/icmp/block', methods=['POST'])
        def block_icmp():
            """Block ICMP from IP"""
            data = request.get_json()
            if not data or 'ip' not in data:
                return jsonify({'error': 'IP address required'}), 400

            ip = data['ip']
            success = self.p4_controller.block_icmp_from_ip(ip)
            if success:
                self.metrics.record_defense_action('block_icmp')
                return jsonify({'status': 'success', 'ip': ip})
            return jsonify({'status': 'error'}), 500

        @self.app.route('/api/icmp/unblock/<ip>', methods=['DELETE'])
        def unblock_icmp(ip):
            """Unblock ICMP from IP"""
            success = self.p4_controller.unblock_icmp_from_ip(ip)
            if success:
                return jsonify({'status': 'success', 'ip': ip})
            return jsonify({'status': 'error'}), 500

        @self.app.route('/api/udp/rate-limited', methods=['GET'])
        def get_udp_rate_limited():
            """Get UDP rate limited IPs"""
            limited = self.p4_controller.get_udp_rate_limited_ips()
            return jsonify({
                'count': len(limited),
                'entries': limited
            })

        @self.app.route('/api/udp/rate-limit', methods=['POST'])
        def set_udp_rate_limit():
            """Set UDP rate limit for IP"""
            data = request.get_json()
            if not data or 'ip' not in data:
                return jsonify({'error': 'IP address required'}), 400

            ip = data['ip']
            pps = data.get('pps', 100)
            success = self.p4_controller.limit_udp_rate(ip, pps)
            if success:
                self.metrics.record_defense_action('set_udp_rate_limit')
                return jsonify({'status': 'success', 'ip': ip, 'pps': pps})
            return jsonify({'status': 'error'}), 500

        @self.app.route('/api/udp/rate-limit/<ip>', methods=['DELETE'])
        def remove_udp_rate_limit(ip):
            """Remove UDP rate limit"""
            success = self.p4_controller.unlimit_udp_rate(ip)
            if success:
                return jsonify({'status': 'success', 'ip': ip})
            return jsonify({'status': 'error'}), 500

        @self.app.route('/api/syn/blocked', methods=['GET'])
        def get_syn_blocked():
            """Get SYN blocked IPs"""
            blocked = self.p4_controller.get_syn_blocked_ips()
            return jsonify({
                'count': len(blocked),
                'entries': blocked
            })

        @self.app.route('/api/syn/block', methods=['POST'])
        def block_syn():
            """Block SYN from IP"""
            data = request.get_json()
            if not data or 'ip' not in data:
                return jsonify({'error': 'IP address required'}), 400

            ip = data['ip']
            success = self.p4_controller.block_syn_flood(ip)
            if success:
                self.metrics.record_defense_action('block_syn')
                return jsonify({'status': 'success', 'ip': ip})
            return jsonify({'status': 'error'}), 500

        @self.app.route('/api/syn/unblock/<ip>', methods=['DELETE'])
        def unblock_syn(ip):
            """Unblock SYN from IP"""
            success = self.p4_controller.unblock_syn_flood(ip)
            if success:
                return jsonify({'status': 'success', 'ip': ip})
            return jsonify({'status': 'error'}), 500

        @self.app.route('/api/meter/l1', methods=['GET'])
        def get_l1_meters():
            """Get all L1 meter configurations"""
            meters = self.p4_controller.get_l1_meter_stats()
            return jsonify({
                'count': len(meters),
                'entries': meters
            })

        @self.app.route('/api/meter/l1', methods=['POST'])
        def configure_l1_meter():
            """Configure L1 meter"""
            data = request.get_json()
            if not data or 'src_ip' not in data or 'protocol' not in data:
                return jsonify({'error': 'src_ip and protocol required'}), 400

            src_ip = data['src_ip']
            protocol_str = data['protocol'].upper()
            threshold_pps = data.get('threshold_pps', 100)

            try:
                protocol = ProtocolType[protocol_str]
            except KeyError:
                return jsonify({'error': f'Invalid protocol: {protocol_str}. Use UDP, ICMP, or TCP_SYN'}), 400

            success = self.p4_controller.configure_l1_meter(src_ip, protocol, threshold_pps)
            if success:
                self.metrics.record_defense_action(f'configure_l1_{protocol_str.lower()}')
                return jsonify({
                    'status': 'success',
                    'src_ip': src_ip,
                    'protocol': protocol_str,
                    'threshold_pps': threshold_pps
                })
            return jsonify({'status': 'error', 'message': 'Failed to configure L1 meter'}), 500

        @self.app.route('/api/meter/l1', methods=['DELETE'])
        def remove_l1_meter():
            """Remove L1 meter configuration"""
            data = request.get_json()
            if not data or 'src_ip' not in data or 'protocol' not in data:
                return jsonify({'error': 'src_ip and protocol required'}), 400

            src_ip = data['src_ip']
            protocol_str = data['protocol'].upper()

            try:
                protocol = ProtocolType[protocol_str]
            except KeyError:
                return jsonify({'error': f'Invalid protocol: {protocol_str}. Use UDP, ICMP, or TCP_SYN'}), 400

            success = self.p4_controller.remove_l1_meter(src_ip, protocol)
            if success:
                return jsonify({'status': 'success', 'src_ip': src_ip, 'protocol': protocol_str})
            return jsonify({'status': 'error', 'message': 'Failed to remove L1 meter'}), 500

        @self.app.route('/api/meter/l2', methods=['GET'])
        def get_l2_meters():
            """Get all L2 meter configurations"""
            meters = self.p4_controller.get_l2_meter_stats()
            return jsonify({
                'count': len(meters),
                'entries': meters
            })

        @self.app.route('/api/meter/l2', methods=['POST'])
        def configure_l2_meter():
            """Configure L2 meter (src_ip + dst_port)"""
            data = request.get_json()
            if not data or 'src_ip' not in data or 'dst_port' not in data or 'protocol' not in data:
                return jsonify({'error': 'src_ip, dst_port, and protocol required'}), 400

            src_ip = data['src_ip']
            dst_port = data['dst_port']
            protocol_str = data['protocol'].upper()
            threshold_pps = data.get('threshold_pps', 50)

            try:
                protocol = ProtocolType[protocol_str]
            except KeyError:
                return jsonify({'error': f'Invalid protocol: {protocol_str}. Use UDP, ICMP, or TCP_SYN'}), 400

            success = self.p4_controller.configure_l2_meter(src_ip, dst_port, protocol, threshold_pps)
            if success:
                self.metrics.record_defense_action(f'configure_l2_{protocol_str.lower()}')
                return jsonify({
                    'status': 'success',
                    'src_ip': src_ip,
                    'dst_port': dst_port,
                    'protocol': protocol_str,
                    'threshold_pps': threshold_pps
                })
            return jsonify({'status': 'error', 'message': 'Failed to configure L2 meter'}), 500

        @self.app.route('/api/meter/l2', methods=['DELETE'])
        def remove_l2_meter():
            """Remove L2 meter configuration"""
            data = request.get_json()
            if not data or 'src_ip' not in data or 'dst_port' not in data or 'protocol' not in data:
                return jsonify({'error': 'src_ip, dst_port, and protocol required'}), 400

            src_ip = data['src_ip']
            dst_port = data['dst_port']
            protocol_str = data['protocol'].upper()

            try:
                protocol = ProtocolType[protocol_str]
            except KeyError:
                return jsonify({'error': f'Invalid protocol: {protocol_str}. Use UDP, ICMP, or TCP_SYN'}), 400

            success = self.p4_controller.remove_l2_meter(src_ip, dst_port, protocol)
            if success:
                return jsonify({
                    'status': 'success',
                    'src_ip': src_ip,
                    'dst_port': dst_port,
                    'protocol': protocol_str
                })
            return jsonify({'status': 'error', 'message': 'Failed to remove L2 meter'}), 500

        @self.app.route('/api/meter/stats', methods=['GET'])
        def get_meter_stats():
            """Get comprehensive meter statistics"""
            stats = self.p4_controller.get_meter_stats()
            return jsonify(stats)

        # ML Adaptive Threshold Endpoints
        @self.app.route('/api/ml/status', methods=['GET'])
        def get_ml_status():
            """Get ML model and adaptive manager status"""
            if not self.ml_model or not self.adaptive_manager:
                return jsonify({'error': 'ML components not configured'}), 404

            return jsonify({
                'model_status': self.ml_model.get_model_status(),
                'adaptive_status': self.adaptive_manager.get_status(),
                'current_thresholds': self.adaptive_manager.get_current_thresholds(),
                'threshold_bounds': self.adaptive_manager.get_threshold_bounds()
            })

        @self.app.route('/api/ml/thresholds', methods=['GET'])
        def get_dynamic_thresholds():
            """Get current dynamic thresholds from ML model"""
            if not self.ml_model:
                return jsonify({'error': 'ML model not configured'}), 404

            return jsonify({
                'current': self.ml_model.get_all_thresholds(),
                'pending': self.ml_model.get_pending_thresholds()
            })

        @self.app.route('/api/ml/thresholds/approve', methods=['POST'])
        def approve_threshold():
            """Approve a pending threshold"""
            if not self.ml_model:
                return jsonify({'error': 'ML model not configured'}), 404

            data = request.get_json()
            if not data or 'threshold_type' not in data:
                return jsonify({'error': 'threshold_type required'}), 400

            type_name = data['threshold_type'].upper()
            try:
                threshold_type = ThresholdType[type_name]
            except KeyError:
                return jsonify({'error': f'Invalid threshold type: {type_name}'}), 400

            success = self.ml_model.approve_threshold(threshold_type)
            if success:
                self.metrics.record_defense_action(f'approve_{type_name}')
                return jsonify({'status': 'success', 'threshold_type': type_name})
            return jsonify({'status': 'error', 'message': 'No pending threshold'}), 404

        @self.app.route('/api/ml/thresholds/reject', methods=['POST'])
        def reject_threshold():
            """Reject a pending threshold"""
            if not self.ml_model:
                return jsonify({'error': 'ML model not configured'}), 404

            data = request.get_json()
            if not data or 'threshold_type' not in data:
                return jsonify({'error': 'threshold_type required'}), 400

            type_name = data['threshold_type'].upper()
            try:
                threshold_type = ThresholdType[type_name]
            except KeyError:
                return jsonify({'error': f'Invalid threshold type: {type_name}'}), 400

            success = self.ml_model.reject_threshold(threshold_type)
            if success:
                return jsonify({'status': 'success', 'threshold_type': type_name})
            return jsonify({'status': 'error', 'message': 'No pending threshold'}), 404

        @self.app.route('/api/ml/mode', methods=['POST'])
        def set_ml_mode():
            """Set ML operation mode (auto/manual)"""
            if not self.ml_model:
                return jsonify({'error': 'ML model not configured'}), 404

            data = request.get_json()
            if not data or 'mode' not in data:
                return jsonify({'error': 'mode required (auto/manual)'}), 400

            mode_str = data['mode'].lower()
            try:
                mode = ModelMode(mode_str)
            except ValueError:
                return jsonify({'error': 'Invalid mode. Use auto or manual'}), 400

            self.ml_model.set_mode(mode)
            return jsonify({'status': 'success', 'mode': mode_str})

        @self.app.route('/api/ml/retrain', methods=['POST'])
        def force_retrain():
            """Force immediate model retraining"""
            if not self.adaptive_manager:
                return jsonify({'error': 'Adaptive manager not configured'}), 404

            self.adaptive_manager.force_retrain()
            return jsonify({'status': 'success', 'message': 'Retraining triggered'})

        @self.app.route('/api/ml/threshold-bounds', methods=['POST'])
        def set_threshold_bounds():
            """Set threshold min/max bounds"""
            if not self.adaptive_manager:
                return jsonify({'error': 'Adaptive manager not configured'}), 404

            data = request.get_json()
            if not data or 'threshold_type' not in data:
                return jsonify({'error': 'threshold_type required'}), 400

            type_name = data['threshold_type'].upper()
            try:
                threshold_type = ThresholdType[type_name]
            except KeyError:
                return jsonify({'error': f'Invalid threshold type: {type_name}'}), 400

            min_val = data.get('min')
            max_val = data.get('max')

            if min_val is not None and max_val is not None:
                self.adaptive_manager.set_threshold_bounds(threshold_type, min_val, max_val)
                return jsonify({
                    'status': 'success',
                    'threshold_type': type_name,
                    'min': min_val,
                    'max': max_val
                })
            return jsonify({'error': 'min and max values required'}), 400

        # Traffic Statistics Endpoints
        @self.app.route('/api/traffic/stats', methods=['GET'])
        def get_traffic_stats():
            """Get current traffic statistics"""
            if not self.traffic_collector:
                return jsonify({'error': 'Traffic collector not configured'}), 404

            return jsonify(self.traffic_collector.get_current_stats())

        @self.app.route('/api/traffic/top-talkers', methods=['GET'])
        def get_top_talkers():
            """Get top talkers"""
            if not self.traffic_collector:
                return jsonify({'error': 'Traffic collector not configured'}), 404

            limit = request.args.get('limit', 10, type=int)
            return jsonify({
                'count': limit,
                'entries': self.traffic_collector.get_top_talkers(limit)
            })

        @self.app.route('/api/traffic/history', methods=['GET'])
        def get_traffic_history():
            """Get traffic feature history"""
            if not self.traffic_collector:
                return jsonify({'error': 'Traffic collector not configured'}), 404

            features = self.traffic_collector.get_all_features()
            return jsonify({
                'sample_count': features['sample_count'],
                'packet_rates': features['packet_rates'][-100:],
                'connection_rates': features['connection_rates'][-100:],
                'protocol_distributions': features['protocol_distributions'][-100:]
            })

        @self.app.route('/api/traffic/ip/<ip>', methods=['GET'])
        def get_ip_stats(ip):
            """Get statistics for a specific IP"""
            if not self.traffic_collector:
                return jsonify({'error': 'Traffic collector not configured'}), 404

            stats = self.traffic_collector.get_per_ip_stats(ip)
            if stats:
                return jsonify(stats)
            return jsonify({'error': 'IP not found'}), 404

        @self.app.route('/api/policy', methods=['GET'])
        def get_policy():
            """Get current defense policy configuration"""
            policy = {
                'auto_blocker': self.auto_blocker.get_status(),
                'thresholds': self.auto_blocker.traffic_monitor._thresholds,
                'block_duration': self.auto_blocker.traffic_monitor._block_duration
            }
            if self.adaptive_manager:
                policy['adaptive_thresholds'] = self.adaptive_manager.get_current_thresholds()
            return jsonify(policy)

        @self.app.route('/api/policy', methods=['POST'])
        def update_policy():
            """Update defense policy"""
            data = request.get_json()
            if not data:
                return jsonify({'error': 'Policy data required'}), 400

            self.auto_blocker.configure(
                udp_pps=data.get('udp_pps', 100),
                icmp_pps=data.get('icmp_pps', 50),
                syn_pps=data.get('syn_pps', 200),
                window_seconds=data.get('window_seconds', 1),
                block_duration=data.get('block_duration', 300),
                l2_udp_pps=data.get('l2_udp_pps', 50),
                l2_icmp_pps=data.get('l2_icmp_pps', 25),
                l2_syn_pps=data.get('l2_syn_pps', 100)
            )

            return jsonify({'status': 'success', 'message': 'Policy updated'})

        @self.app.route('/api/stats', methods=['GET'])
        def get_stats():
            """Get defense statistics"""
            drop_stats = self.metrics.get_drop_stats()
            meter_stats = self.p4_controller.get_meter_stats()
            stats = {
                'drop_stats': drop_stats,
                'blacklist_count': len(self.p4_controller.get_blacklist_ips()),
                'udp_rate_limited_count': len(self.p4_controller.get_udp_rate_limited_ips()),
                'icmp_blocked_count': len(self.p4_controller.get_icmp_blocked_ips()),
                'syn_blocked_count': len(self.p4_controller.get_syn_blocked_ips()),
                'meter_stats': meter_stats
            }
            if self.traffic_collector:
                stats['traffic_stats'] = self.traffic_collector.get_statistics()
            return jsonify(stats)

        @self.app.route('/metrics', methods=['GET'])
        def metrics():
            """Prometheus metrics endpoint"""
            return Response(
                self.metrics.get_metrics(),
                mimetype='text/plain; version=0.0.4'
            )

        @self.app.route('/api/forwarding', methods=['POST'])
        def add_forwarding():
            """Add forwarding rule"""
            data = request.get_json()
            if not data or 'dst_ip' not in data or 'port' not in data:
                return jsonify({'error': 'dst_ip and port required'}), 400

            dst_ip = data['dst_ip']
            prefix_len = data.get('prefix_len', 32)
            port = data['port']

            success = self.p4_controller.add_forwarding_rule(
                dst_ip, prefix_len, port
            )
            if success:
                return jsonify({'status': 'success'})
            return jsonify({'status': 'error'}), 500

        @self.app.errorhandler(404)
        def not_found(e):
            return jsonify({'error': 'Not found'}), 404

        @self.app.errorhandler(500)
        def internal_error(e):
            return jsonify({'error': 'Internal server error'}), 500

    def run(self):
        """Start the REST API server"""
        logger.info(f"Starting REST API on {self.host}:{self.port}")
        self.app.run(host=self.host, port=self.port, debug=False)
