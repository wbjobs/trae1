"""RTP 音视频质量监控系统 - 入口

运行：
    python main.py [--keylog KEYLOG_FILE] [--port-start START] [--port-end END] [--mos-threshold MOS]
"""

import argparse
import sys
import logging

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)


def main():
    parser = argparse.ArgumentParser(
        description="RTP Audio/Video Quality Monitor (with SRTP support)"
    )
    parser.add_argument(
        "--keylog", type=str, default=None,
        help="Path to Wireshark-format keylog file (SSLKEYLOG / SRTP_MASTER_KEY)"
    )
    parser.add_argument(
        "--port-start", type=int, default=5000,
        help="Start of UDP port range to monitor (default: 5000)"
    )
    parser.add_argument(
        "--port-end", type=int, default=6000,
        help="End of UDP port range to monitor (default: 6000)"
    )
    parser.add_argument(
        "--mos-threshold", type=float, default=3.0,
        help="MOS alert threshold below which to capture screenshots/recordings (default: 3.0)"
    )
    parser.add_argument(
        "--no-gui", action="store_true",
        help="Run without PyQt GUI (headless mode, Web Dashboard only)"
    )
    args = parser.parse_args()

    if args.no_gui:
        _run_headless(args)
    else:
        _run_gui(args)


def _run_gui(args):
    from PyQt5.QtWidgets import QApplication
    from backend.app import MonitorApp

    qt_app = QApplication(sys.argv)
    window = MonitorApp(keylog_path=args.keylog)
    window._port_start = args.port_start
    window._port_end = args.port_end
    window._mos_threshold = args.mos_threshold
    window._port_start_spin.setValue(args.port_start)
    window._port_end_spin.setValue(args.port_end)
    window._mos_spin.setValue(args.mos_threshold)
    window.show()
    sys.exit(qt_app.exec_())


def _run_headless(args):
    import asyncio
    from backend.capture import CaptureManager
    from backend.keylog import KeylogParser
    from backend.rtp_listener import RtpListenerManager
    from backend.rtp_parser import RtpPacket
    from backend.srtp import SrtpManager
    from backend.web_api import DashboardServer
    from backend.window import StreamRegistry

    async def _run():
        srtp = SrtpManager()
        if args.keylog:
            parser = KeylogParser.parse_file(args.keylog)
            parser.apply_to_manager(srtp)
            logging.info("Loaded %d SRTP keys from keylog: %s",
                         len(srtp.get_all_contexts()), args.keylog)

        registry = StreamRegistry(mos_alert_threshold=args.mos_threshold)
        capture = CaptureManager()

        async def on_packet(pkt: RtpPacket, host: str, port: int) -> None:
            capture.feed_packet(pkt)
            await registry.feed_packet(pkt, host, port)

        listener = RtpListenerManager(
            port_range=range(args.port_start, args.port_end + 1),
            bind_addr="0.0.0.0",
            on_packet=on_packet,
            srtp_manager=srtp,
        )
        await listener.start()

        dashboard = DashboardServer(registry, host="0.0.0.0", port=8080)
        await dashboard.start()

        logging.info("RTP Monitor running (headless mode)")
        logging.info("  Ports: %d-%d, Dashboard: http://localhost:8080",
                     args.port_start, args.port_end)
        if srtp.get_all_contexts():
            logging.info("  SRTP contexts: %d", len(srtp.get_all_contexts()))

        try:
            while True:
                await asyncio.sleep(60.0)
                registry.cleanup_stale()
                srtp.cleanup_stale()
        except KeyboardInterrupt:
            logging.info("Shutting down...")
        finally:
            listener.stop()
            await dashboard.stop()

    asyncio.run(_run())


if __name__ == "__main__":
    main()
