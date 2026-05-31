"""
Main Controller for DDoS Defense Gateway v3
Integrates all components: P4 controller, API server, auto blocker, metrics, ML adaptive thresholds
"""

import time
import threading
import logging
import signal
import sys
import os

from .p4_controller import P4Controller
from .metrics import MetricsCollector
from .auto_blocker import AutoBlocker
from .api_server import DefenseAPI
from .traffic_collector import TrafficCollector
from .ml_model import IsolationForestModel, ModelMode
from .adaptive_manager import AdaptiveManager

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class DDoSDefenseGateway:
    """Main DDoS Defense Gateway controller with ML adaptive thresholds"""

    def __init__(self, config: dict = None):
        self.config = config or self._default_config()

        self.p4_controller = P4Controller(
            grpc_addr=self.config['grpc_addr'],
            device_id=self.config['device_id'],
            p4info_path=self.config['p4info_path'],
            bmv2_json_path=self.config['bmv2_json_path']
        )

        self.metrics = MetricsCollector()

        self.traffic_collector = TrafficCollector(
            window_size=self.config.get('traffic_window_size', 300),
            sample_interval=self.config.get('traffic_sample_interval', 60)
        )

        self.ml_model = IsolationForestModel(
            n_estimators=self.config.get('ml_n_estimators', 100),
            contamination=self.config.get('ml_contamination', 0.1)
        )

        ml_mode = ModelMode[self.config.get('ml_mode', 'AUTO').upper()]
        self.ml_model.set_mode(ml_mode)

        self.auto_blocker = AutoBlocker(self.p4_controller, self.metrics)

        self.adaptive_manager = AdaptiveManager(
            self.p4_controller,
            self.traffic_collector,
            self.ml_model
        )

        self.api_server = DefenseAPI(
            self.p4_controller,
            self.metrics,
            self.auto_blocker,
            adaptive_manager=self.adaptive_manager,
            traffic_collector=self.traffic_collector,
            ml_model=self.ml_model,
            host=self.config['api_host'],
            port=self.config['api_port']
        )

        self._running = False
        self._api_thread: threading.Thread = None

    def _default_config(self) -> dict:
        """Get default configuration"""
        return {
            'grpc_addr': os.environ.get('P4GRPC_ADDR', 'localhost:50051'),
            'device_id': int(os.environ.get('P4DEVICE_ID', '0')),
            'p4info_path': os.environ.get('P4INFO_PATH', './p4/ddos_defense.p4info'),
            'bmv2_json_path': os.environ.get('BMV2_JSON_PATH', './p4/ddos_defense.json'),
            'api_host': os.environ.get('API_HOST', '0.0.0.0'),
            'api_port': int(os.environ.get('API_PORT', '8080')),
            'udp_threshold': int(os.environ.get('UDP_THRESHOLD', '100')),
            'icmp_threshold': int(os.environ.get('ICMP_THRESHOLD', '50')),
            'syn_threshold': int(os.environ.get('SYN_THRESHOLD', '200')),
            'block_duration': int(os.environ.get('BLOCK_DURATION', '300')),
            'ml_mode': os.environ.get('ML_MODE', 'auto'),
            'ml_n_estimators': int(os.environ.get('ML_N_ESTIMATORS', '100')),
            'ml_contamination': float(os.environ.get('ML_CONTAMINATION', '0.1')),
            'traffic_window_size': int(os.environ.get('TRAFFIC_WINDOW_SIZE', '300')),
            'traffic_sample_interval': int(os.environ.get('TRAFFIC_SAMPLE_INTERVAL', '60')),
        }

    def start(self):
        """Start the DDoS Defense Gateway"""
        logger.info("=" * 60)
        logger.info("Starting DDoS Defense Gateway v3 with ML Adaptive Thresholds")
        logger.info("=" * 60)

        logger.info("Connecting to BMv2 switch...")
        if not self.p4_controller.connect():
            logger.error("Failed to connect to BMv2 switch")
            return False

        self._install_default_rules()

        logger.info("Starting traffic collector...")
        self.traffic_collector.start()

        logger.info("Configuring auto-blocker...")
        self.auto_blocker.configure(
            udp_pps=self.config['udp_threshold'],
            icmp_pps=self.config['icmp_threshold'],
            syn_pps=self.config['syn_threshold'],
            block_duration=self.config['block_duration']
        )
        self.auto_blocker.start()

        logger.info("Starting adaptive threshold manager...")
        self.adaptive_manager.start()

        self.metrics.update_ml_mode(self.config.get('ml_mode', 'auto'))

        self._api_thread = threading.Thread(target=self._run_api, daemon=True)
        self._api_thread.start()

        self._running = True
        logger.info("DDoS Defense Gateway started successfully")
        logger.info(f"REST API: http://{self.config['api_host']}:{self.config['api_port']}")
        logger.info(f"ML Mode: {self.config.get('ml_mode', 'auto')}")
        return True

    def _run_api(self):
        """Run API server in a separate thread"""
        try:
            self.api_server.run()
        except Exception as e:
            logger.error(f"API server error: {e}")

    def _install_default_rules(self):
        """Install default forwarding rules"""
        logger.info("Installing default forwarding rules...")

        rules = [
            ('10.0.0.0', 24, 1),
            ('10.0.1.0', 24, 2),
            ('10.0.2.0', 24, 3),
        ]

        for dst_ip, prefix_len, port in rules:
            try:
                self.p4_controller.add_forwarding_rule(dst_ip, prefix_len, port)
            except Exception as e:
                logger.warning(f"Failed to install rule {dst_ip}/{prefix_len}: {e}")

        logger.info("Default rules installed")

    def stop(self):
        """Stop the DDoS Defense Gateway"""
        logger.info("Stopping DDoS Defense Gateway...")

        self._running = False
        self.adaptive_manager.stop()
        self.auto_blocker.stop()
        self.traffic_collector.stop()

        if self.p4_controller.is_connected():
            self.p4_controller.disconnect()

        logger.info("DDoS Defense Gateway stopped")

    def run(self):
        """Run the gateway main loop"""
        if not self.start():
            sys.exit(1)

        def signal_handler(sig, frame):
            logger.info("Received shutdown signal")
            self.stop()
            sys.exit(0)

        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)

        try:
            while self._running:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("Keyboard interrupt received")
        finally:
            self.stop()


def main():
    """Main entry point"""
    import argparse

    parser = argparse.ArgumentParser(description='DDoS Defense Gateway v3')
    parser.add_argument('--grpc-addr', default='localhost:50051',
                        help='P4Runtime gRPC address')
    parser.add_argument('--device-id', type=int, default=0,
                        help='BMv2 device ID')
    parser.add_argument('--p4info', default='./p4/ddos_defense.p4info',
                        help='Path to P4Info file')
    parser.add_argument('--bmv2-json', default='./p4/ddos_defense.json',
                        help='Path to BMv2 JSON file')
    parser.add_argument('--api-host', default='0.0.0.0',
                        help='REST API host')
    parser.add_argument('--api-port', type=int, default=8080,
                        help='REST API port')
    parser.add_argument('--udp-threshold', type=int, default=100,
                        help='UDP rate threshold (pps)')
    parser.add_argument('--icmp-threshold', type=int, default=50,
                        help='ICMP rate threshold (pps)')
    parser.add_argument('--syn-threshold', type=int, default=200,
                        help='SYN rate threshold (pps)')
    parser.add_argument('--block-duration', type=int, default=300,
                        help='Block duration in seconds')
    parser.add_argument('--ml-mode', choices=['auto', 'manual'], default='auto',
                        help='ML operation mode')
    parser.add_argument('--ml-n-estimators', type=int, default=100,
                        help='Number of trees in Isolation Forest')
    parser.add_argument('--ml-contamination', type=float, default=0.1,
                        help='Expected contamination ratio')

    args = parser.parse_args()

    config = {
        'grpc_addr': args.grpc_addr,
        'device_id': args.device_id,
        'p4info_path': args.p4info,
        'bmv2_json_path': args.bmv2_json,
        'api_host': args.api_host,
        'api_port': args.api_port,
        'udp_threshold': args.udp_threshold,
        'icmp_threshold': args.icmp_threshold,
        'syn_threshold': args.syn_threshold,
        'block_duration': args.block_duration,
        'ml_mode': args.ml_mode,
        'ml_n_estimators': args.ml_n_estimators,
        'ml_contamination': args.ml_contamination,
    }

    gateway = DDoSDefenseGateway(config)
    gateway.run()


if __name__ == '__main__':
    main()
