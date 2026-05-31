import logging
import time
from typing import List, Dict, Any, Optional
from datetime import datetime
import psutil

logger = logging.getLogger(__name__)


class NetworkCollector:
    _last_net_io: Dict[str, Any] = {}
    _last_collect_time: float = 0

    @staticmethod
    def collect() -> List[Dict[str, Any]]:
        try:
            networks = []
            net_io = psutil.net_io_counters(pernic=True)
            current_time = time.time()

            skip_interfaces = [
                "lo", "Loopback",
                "veth", "br-", "docker", "docker0",
                "vboxnet", "virbr", "vmnet",
                "tun", "tap", "ppp"
            ]

            for interface, stats in net_io.items():
                if any(interface.startswith(prefix) for prefix in skip_interfaces):
                    continue

                try:
                    bytes_sent = getattr(stats, 'bytes_sent', 0)
                    bytes_recv = getattr(stats, 'bytes_recv', 0)
                    packets_sent = getattr(stats, 'packets_sent', 0)
                    packets_recv = getattr(stats, 'packets_recv', 0)
                    errors_in = getattr(stats, 'errin', 0)
                    errors_out = getattr(stats, 'errout', 0)
                    drops_in = getattr(stats, 'dropin', 0)
                    drops_out = getattr(stats, 'dropout', 0)

                    net_data = {
                        "interface": interface,
                        "bytes_sent": bytes_sent,
                        "bytes_recv": bytes_recv,
                        "bytes_sent_rate": 0.0,
                        "bytes_recv_rate": 0.0,
                        "packets_sent": packets_sent,
                        "packets_recv": packets_recv,
                        "packets_sent_rate": 0.0,
                        "packets_recv_rate": 0.0,
                        "errors_in": errors_in,
                        "errors_out": errors_out,
                        "errors_in_rate": 0.0,
                        "errors_out_rate": 0.0,
                        "drops_in": drops_in,
                        "drops_out": drops_out,
                        "drops_in_rate": 0.0,
                        "drops_out_rate": 0.0,
                        "timestamp": datetime.utcnow().isoformat()
                    }

                    last_io = NetworkCollector._last_net_io.get(interface)
                    last_time = NetworkCollector._last_collect_time
                    if last_io and last_time > 0:
                        elapsed = current_time - last_time
                        if elapsed > 0:
                            net_data["bytes_sent_rate"] = max(0, (bytes_sent - last_io.get('bytes_sent', 0)) / elapsed)
                            net_data["bytes_recv_rate"] = max(0, (bytes_recv - last_io.get('bytes_recv', 0)) / elapsed)
                            net_data["packets_sent_rate"] = max(0, (packets_sent - last_io.get('packets_sent', 0)) / elapsed)
                            net_data["packets_recv_rate"] = max(0, (packets_recv - last_io.get('packets_recv', 0)) / elapsed)
                            net_data["errors_in_rate"] = max(0, (errors_in - last_io.get('errors_in', 0)) / elapsed)
                            net_data["errors_out_rate"] = max(0, (errors_out - last_io.get('errors_out', 0)) / elapsed)
                            net_data["drops_in_rate"] = max(0, (drops_in - last_io.get('drops_in', 0)) / elapsed)
                            net_data["drops_out_rate"] = max(0, (drops_out - last_io.get('drops_out', 0)) / elapsed)

                    NetworkCollector._last_net_io[interface] = {
                        'bytes_sent': bytes_sent,
                        'bytes_recv': bytes_recv,
                        'packets_sent': packets_sent,
                        'packets_recv': packets_recv,
                        'errors_in': errors_in,
                        'errors_out': errors_out,
                        'drops_in': drops_in,
                        'drops_out': drops_out
                    }

                    networks.append(net_data)
                except Exception as e:
                    logger.warning(f"处理网络接口 {interface} 失败: {e}")
                    continue

            NetworkCollector._last_collect_time = current_time

            logger.debug(f"网络指标采集成功, 接口数量: {len(networks)}")
            return networks
        except Exception as e:
            logger.error(f"网络指标采集失败: {e}")
            raise

    @staticmethod
    def collect_connections() -> Dict[str, Any]:
        try:
            connections = psutil.net_connections(kind='inet')
            result = {
                "total_connections": len(connections),
                "listening": 0,
                "established": 0,
                "time_wait": 0,
                "close_wait": 0,
                "timestamp": datetime.utcnow().isoformat()
            }
            for conn in connections:
                status = conn.status
                if status == 'LISTEN':
                    result["listening"] += 1
                elif status == 'ESTABLISHED':
                    result["established"] += 1
                elif status == 'TIME_WAIT':
                    result["time_wait"] += 1
                elif status == 'CLOSE_WAIT':
                    result["close_wait"] += 1
            return result
        except Exception as e:
            logger.error(f"网络连接指标采集失败: {e}")
            return {}

    @classmethod
    def reset_cache(cls):
        cls._last_net_io.clear()
        cls._last_collect_time = 0
