"""Main entry point for RabbitMQ Audit Gateway"""
import os
import sys
import signal
import logging
import argparse
from typing import Optional

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


def parse_args():
    parser = argparse.ArgumentParser(description='RabbitMQ Audit Gateway')
    parser.add_argument('--config', '-c', default='config/config.yaml', help='Path to config file')
    parser.add_argument('--mode', '-m', choices=['gateway', 'dashboard', 'all'], default='all', help='Run mode')
    parser.add_argument('--log-level', '-l', default='INFO', choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'])
    parser.add_argument('--max-body-size', type=int, default=None, help='Maximum message body size in bytes for content audit (default: 1MB)')
    parser.add_argument('--scan-threads', type=int, default=None, help='Number of virus scan threads (default: 4)')
    parser.add_argument('--enable-virus-scan', action='store_true', default=None, help='Enable virus scanning')
    parser.add_argument('--disable-virus-scan', action='store_true', default=None, help='Disable virus scanning')
    parser.add_argument('--scan-timeout', type=int, default=None, help='Virus scan timeout in seconds (default: 5)')
    parser.add_argument('--clamav-host', type=str, default=None, help='ClamAV server host')
    parser.add_argument('--clamav-port', type=int, default=None, help='ClamAV server port')
    parser.add_argument('--yara-rules-dir', type=str, default=None, help='YARA rules directory')
    return parser.parse_args()


class GatewayApplication:
    def __init__(
        self,
        config_path: str,
        max_body_size: Optional[int] = None,
        scan_threads: Optional[int] = None,
        enable_virus_scan: Optional[bool] = None,
        scan_timeout: Optional[int] = None,
        clamav_host: Optional[str] = None,
        clamav_port: Optional[int] = None,
        yara_rules_dir: Optional[str] = None
    ):
        self.config_path = config_path
        self.max_body_size = max_body_size
        self.scan_threads = scan_threads
        self.enable_virus_scan = enable_virus_scan
        self.scan_timeout = scan_timeout
        self.clamav_host = clamav_host
        self.clamav_port = clamav_port
        self.yara_rules_dir = yara_rules_dir
        self.gateway = None
        self.dashboard = None
        self._running = False

    def _load_config(self):
        from .config import load_config
        os.environ['CONFIG_PATH'] = self.config_path
        config = load_config(self.config_path)

        # 如果命令行指定了max-body-size，覆盖配置文件的值
        if self.max_body_size is not None:
            config.audit.max_body_size = self.max_body_size
            logger.info(f"Using command line max-body-size: {self.max_body_size} bytes")

        # 病毒扫描相关参数
        if hasattr(config, 'virus_scan'):
            if self.enable_virus_scan is not None:
                config.virus_scan.enabled = self.enable_virus_scan
                logger.info(f"Virus scan {'enabled' if self.enable_virus_scan else 'disabled'} via command line")

            if self.scan_threads is not None:
                config.virus_scan.max_threads = self.scan_threads
                logger.info(f"Using command line scan-threads: {self.scan_threads}")

            if self.scan_timeout is not None:
                config.virus_scan.scan_timeout = self.scan_timeout
                logger.info(f"Using command line scan-timeout: {self.scan_timeout}s")

            if self.clamav_host is not None and hasattr(config.virus_scan, 'clamav'):
                config.virus_scan.clamav.host = self.clamav_host
                config.virus_scan.clamav.enabled = True
                logger.info(f"Using ClamAV host: {self.clamav_host}")

            if self.clamav_port is not None and hasattr(config.virus_scan, 'clamav'):
                config.virus_scan.clamav.port = self.clamav_port
                logger.info(f"Using ClamAV port: {self.clamav_port}")

            if self.yara_rules_dir is not None and hasattr(config.virus_scan, 'yara'):
                config.virus_scan.yara.rules_dir = self.yara_rules_dir
                config.virus_scan.yara.enabled = True
                logger.info(f"Using YARA rules dir: {self.yara_rules_dir}")

        return config

    def _setup_signal_handlers(self):
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

    def _signal_handler(self, signum, frame):
        logger.info(f"Received signal {signum}, shutting down...")
        self.stop()

    def start_gateway(self, config):
        from .gateway import MessageGateway
        logger.info("Starting message gateway...")
        self.gateway = MessageGateway(config)
        self.gateway.start()
        logger.info(f"Gateway {self.gateway.gateway_id} started")

    def start_dashboard(self, config):
        if self.gateway is None:
            raise RuntimeError("Gateway must be started before dashboard")

        from .dashboard import DashboardServer
        logger.info("Starting dashboard server...")
        self.dashboard = DashboardServer(config, self.gateway)
        self.dashboard.start()
        logger.info(f"Dashboard server started on {config.dashboard.host}:{config.dashboard.port}")

    def start(self, mode: str = 'all'):
        self._running = True
        self._setup_signal_handlers()

        config = self._load_config()

        logging.getLogger().setLevel(getattr(logging, config.audit.log_level))

        if mode in ('gateway', 'all'):
            self.start_gateway(config)

        if mode in ('dashboard', 'all'):
            self.start_dashboard(config)

        if self.dashboard:
            logger.info("Application running. Press Ctrl+C to stop.")
            self.dashboard.run()
        else:
            logger.info("Gateway running. Press Ctrl+C to stop.")
            import time
            try:
                while self._running:
                    time.sleep(1)
            except KeyboardInterrupt:
                self.stop()

    def stop(self):
        self._running = False

        if self.dashboard:
            logger.info("Stopping dashboard...")
            self.dashboard.stop()

        if self.gateway:
            logger.info("Stopping gateway...")
            self.gateway.stop()

        logger.info("Application stopped")


def main():
    args = parse_args()

    if args.log_level:
        logging.getLogger().setLevel(getattr(logging, args.log_level))

    # 处理enable/disable virus scan参数
    enable_virus_scan = None
    if args.enable_virus_scan:
        enable_virus_scan = True
    elif args.disable_virus_scan:
        enable_virus_scan = False

    app = GatewayApplication(
        args.config,
        max_body_size=args.max_body_size,
        scan_threads=args.scan_threads,
        enable_virus_scan=enable_virus_scan,
        scan_timeout=args.scan_timeout,
        clamav_host=args.clamav_host,
        clamav_port=args.clamav_port,
        yara_rules_dir=args.yara_rules_dir
    )

    try:
        app.start(mode=args.mode)
    except Exception as e:
        logger.error(f"Application error: {e}", exc_info=True)
        sys.exit(1)


if __name__ == '__main__':
    main()
