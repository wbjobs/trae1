"""PyQt 主窗口

功能：
- 启动/停止 RTP 监听
- 显示流列表与实时指标
- 打开 Web Dashboard 浏览器窗口
- 手动导出 PDF 报表
- 显示日志与截图/录音文件列表
- SRTP 密钥管理（Wireshark keylog 导入、手动配置、状态查看）
"""

from __future__ import annotations

import asyncio
import logging
import sys
import threading
import time
from pathlib import Path
from typing import List, Optional

from PyQt5.QtCore import Qt, QTimer, QUrl
from PyQt5.QtGui import QIcon, QDesktopServices
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QTableWidget, QTableWidgetItem, QHeaderView,
    QTextEdit, QSplitter, QGroupBox, QMessageBox, QTabWidget, QSpinBox,
    QDoubleSpinBox, QFileDialog, QAction, QStatusBar, QListWidget,
    QListWidgetItem, QAbstractItemView, QLineEdit, QComboBox,
    QPlainTextEdit, QFormLayout, QDialog, QCheckBox,
)

from .capture import CaptureManager
from .content_buffer import ContentBuffer
from .dtls_srtp import derive_srtp_keys_from_dtls, add_dtls_srtp_context
from .keylog import KeylogParser
from .quality import StreamAnalyzer, StreamMetrics
from .rtp_listener import RtpListenerManager
from .rtp_parser import RtpPacket
from .report import export_report
from .srtp import SrtpManager, CRYPTO_SUITES, DEFAULT_SUITE
from .ai_analyzer import ContentAnalysisResult
from .web_api import DashboardServer
from .window import StreamRegistry

logger = logging.getLogger(__name__)


class SrtpKeyDialog(QDialog):
    """手动添加 SRTP 密钥的对话框。"""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Add SRTP Key")
        self.setMinimumWidth(480)

        layout = QFormLayout(self)

        self._ssrc_edit = QLineEdit()
        self._ssrc_edit.setPlaceholderText("e.g. 0x12345678 or 305419896")
        layout.addRow("SSRC:", self._ssrc_edit)

        self._role_combo = QComboBox()
        self._role_combo.addItems(["from-client", "from-server", "unknown"])
        layout.addRow("Role:", self._role_combo)

        self._suite_combo = QComboBox()
        self._suite_combo.addItems(list(CRYPTO_SUITES.keys()))
        self._suite_combo.setCurrentText(DEFAULT_SUITE)
        layout.addRow("Crypto Suite:", self._suite_combo)

        self._key_edit = QLineEdit()
        self._key_edit.setPlaceholderText("Master Key (hex, 32 or 64 chars)")
        layout.addRow("Master Key:", self._key_edit)

        self._salt_edit = QLineEdit()
        self._salt_edit.setPlaceholderText("Master Salt (hex, 28 chars)")
        layout.addRow("Master Salt:", self._salt_edit)

        self._header_enc = QCheckBox("Header Encryption (SRE)")
        layout.addRow("", self._header_enc)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        self._ok_btn = QPushButton("Add")
        self._ok_btn.clicked.connect(self.accept)
        self._cancel_btn = QPushButton("Cancel")
        self._cancel_btn.clicked.connect(self.reject)
        btn_layout.addWidget(self._ok_btn)
        btn_layout.addWidget(self._cancel_btn)
        layout.addRow(btn_layout)

    def get_params(self) -> Optional[dict]:
        ssrc_text = self._ssrc_edit.text().strip()
        try:
            ssrc = int(ssrc_text, 0)
        except ValueError:
            QMessageBox.warning(self, "Invalid SSRC", "Please enter a valid SSRC (hex or decimal).")
            return None

        try:
            master_key = bytes.fromhex(self._key_edit.text().strip())
        except ValueError:
            QMessageBox.warning(self, "Invalid Key", "Master Key must be valid hex.")
            return None

        try:
            master_salt = bytes.fromhex(self._salt_edit.text().strip())
        except ValueError:
            QMessageBox.warning(self, "Invalid Salt", "Master Salt must be valid hex.")
            return None

        return {
            "ssrc": ssrc,
            "role": self._role_combo.currentText(),
            "suite": self._suite_combo.currentText(),
            "master_key": master_key,
            "master_salt": master_salt,
            "header_encrypted": self._header_enc.isChecked(),
        }


class MonitorApp(QMainWindow):
    """PyQt 主应用窗口。"""

    def __init__(self, keylog_path: Optional[str] = None) -> None:
        super().__init__()
        self.setWindowTitle("RTP Audio/Video Quality Monitor")
        self.resize(1400, 900)

        # 核心组件（在 start 时初始化）
        self._registry: StreamRegistry = StreamRegistry()
        self._capture: CaptureManager = CaptureManager()
        self._srtp: SrtpManager = SrtpManager()
        self._listener: RtpListenerManager | None = None
        self._dashboard: DashboardServer | None = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._loop_thread: threading.Thread | None = None
        self._running = False
        self._port_start = 5000
        self._port_end = 6000
        self._mos_threshold = 3.0
        self._keylog_path = keylog_path

        # UI
        self._build_ui()

        # 如果有 keylog 文件，预加载
        if keylog_path:
            self._load_keylog(keylog_path)

        # 定时器刷新
        self._refresh_timer = QTimer(self)
        self._refresh_timer.timeout.connect(self._refresh_display)
        self._refresh_timer.start(1000)

    # ---------- UI ----------

    def _build_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(8, 8, 8, 8)

        # 顶部控制栏
        control_bar = QGroupBox("Control")
        ctrl_layout = QHBoxLayout(control_bar)

        ctrl_layout.addWidget(QLabel("Port Range:"))
        self._port_start_spin = QSpinBox()
        self._port_start_spin.setRange(1024, 65535)
        self._port_start_spin.setValue(5000)
        self._port_start_spin.valueChanged.connect(self._on_port_changed)
        ctrl_layout.addWidget(self._port_start_spin)

        self._port_end_spin = QSpinBox()
        self._port_end_spin.setRange(1024, 65535)
        self._port_end_spin.setValue(6000)
        self._port_end_spin.valueChanged.connect(self._on_port_changed)
        ctrl_layout.addWidget(self._port_end_spin)

        ctrl_layout.addWidget(QLabel("MOS Threshold:"))
        self._mos_spin = QDoubleSpinBox()
        self._mos_spin.setRange(1.0, 4.5)
        self._mos_spin.setValue(3.0)
        self._mos_spin.setSingleStep(0.1)
        self._mos_spin.valueChanged.connect(self._on_mos_changed)
        ctrl_layout.addWidget(self._mos_spin)

        self._start_btn = QPushButton("Start Monitoring")
        self._start_btn.clicked.connect(self._toggle_monitoring)
        ctrl_layout.addWidget(self._start_btn)

        self._dashboard_btn = QPushButton("Open Web Dashboard")
        self._dashboard_btn.clicked.connect(self._open_dashboard)
        self._dashboard_btn.setEnabled(False)
        ctrl_layout.addWidget(self._dashboard_btn)

        self._report_btn = QPushButton("Export PDF Report")
        self._report_btn.clicked.connect(self._export_report)
        ctrl_layout.addWidget(self._report_btn)

        self._ai_checkbox = QCheckBox("AI Content Analysis")
        self._ai_checkbox.setChecked(True)
        self._ai_checkbox.stateChanged.connect(self._on_ai_toggled)
        ctrl_layout.addWidget(self._ai_checkbox)

        ctrl_layout.addStretch()
        main_layout.addWidget(control_bar)

        # 主内容区：左侧流列表 + 右侧详情
        splitter = QSplitter(Qt.Horizontal)
        main_layout.addWidget(splitter, 1)

        # 左侧：流列表
        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)
        left_layout.addWidget(QLabel("Active Streams"))
        self._stream_table = QTableWidget(0, 9)
        self._stream_table.setHorizontalHeaderLabels(
            ["SSRC", "Media", "Codec", "Source", "Pkts", "Loss%", "MOS", "SRTP", "Content"]
        )
        self._stream_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._stream_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self._stream_table.itemSelectionChanged.connect(self._on_stream_selected)
        left_layout.addWidget(self._stream_table, 1)
        splitter.addWidget(left_widget)

        # 右侧：Tabs
        right_tabs = QTabWidget()
        splitter.addWidget(right_tabs)

        # Tab 1: 指标详情
        detail_widget = QWidget()
        detail_layout = QVBoxLayout(detail_widget)
        self._detail_label = QLabel("Select a stream to see details")
        self._detail_label.setStyleSheet("font-size: 12pt; color: #666;")
        detail_layout.addWidget(self._detail_label)
        self._detail_table = QTableWidget(0, 9)
        self._detail_table.setHorizontalHeaderLabels(
            ["Time", "Pkts", "Lost", "Loss%", "Jitter(ms)", "Delay(ms)", "MOS", "FPS", "Bitrate(kbps)"]
        )
        self._detail_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._detail_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        detail_layout.addWidget(self._detail_table, 1)
        right_tabs.addTab(detail_widget, "Metrics")

        # Tab 2: 截图/录音
        cap_widget = QWidget()
        cap_layout = QVBoxLayout(cap_widget)
        self._cap_list = QListWidget()
        self._cap_list.itemDoubleClicked.connect(self._open_capture_file)
        cap_layout.addWidget(self._cap_list, 1)
        btn_row = QHBoxLayout()
        self._refresh_cap_btn = QPushButton("Refresh")
        self._refresh_cap_btn.clicked.connect(self._refresh_capture_list)
        btn_row.addWidget(self._refresh_cap_btn)
        btn_row.addStretch()
        cap_layout.addLayout(btn_row)
        right_tabs.addTab(cap_widget, "Captures")

        # Tab 3: AI 内容分析
        ai_widget = QWidget()
        ai_layout = QVBoxLayout(ai_widget)
        self._ai_detail_label = QLabel("Select a stream to see AI analysis details")
        self._ai_detail_label.setStyleSheet("font-size: 12pt; color: #666;")
        ai_layout.addWidget(self._ai_detail_label)
        self._ai_table = QTableWidget(0, 2)
        self._ai_table.setHorizontalHeaderLabels(["Metric", "Value"])
        self._ai_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._ai_table.verticalHeader().setSectionResizeMode(QHeaderView.ResizeToContents)
        self._ai_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        ai_layout.addWidget(self._ai_table, 1)
        right_tabs.addTab(ai_widget, "AI Analysis")

        # Tab 3: SRTP 密钥管理
        srtp_widget = QWidget()
        srtp_layout = QVBoxLayout(srtp_widget)

        # SRTP 操作按钮
        srtp_btn_bar = QHBoxLayout()
        self._import_keylog_btn = QPushButton("Import Keylog File")
        self._import_keylog_btn.clicked.connect(self._import_keylog_dialog)
        srtp_btn_bar.addWidget(self._import_keylog_btn)

        self._add_key_btn = QPushButton("Add Key Manually")
        self._add_key_btn.clicked.connect(self._add_key_manual)
        srtp_btn_bar.addWidget(self._add_key_btn)

        self._clear_keys_btn = QPushButton("Clear All Keys")
        self._clear_keys_btn.clicked.connect(self._clear_srtp_keys)
        srtp_btn_bar.addWidget(self._clear_keys_btn)

        srtp_btn_bar.addStretch()
        srtp_layout.addLayout(srtp_btn_bar)

        # SRTP 密钥列表
        self._srtp_table = QTableWidget(0, 7)
        self._srtp_table.setHorizontalHeaderLabels(
            ["SSRC", "Role", "Suite", "Decrypted", "Auth Failed", "Replay", "Header Enc"]
        )
        self._srtp_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._srtp_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        srtp_layout.addWidget(self._srtp_table, 1)

        # DTLS-SRTP 密钥提取
        dtls_group = QGroupBox("DTLS-SRTP Key Derivation (from known master_secret)")
        dtls_layout = QFormLayout(dtls_group)
        self._dtls_ssrc = QLineEdit()
        self._dtls_ssrc.setPlaceholderText("SSRC (hex or decimal)")
        dtls_layout.addRow("SSRC:", self._dtls_ssrc)
        self._dtls_master_secret = QLineEdit()
        self._dtls_master_secret.setPlaceholderText("master_secret (hex, 96 chars for TLS 1.2)")
        dtls_layout.addRow("Master Secret:", self._dtls_master_secret)
        self._dtls_client_random = QLineEdit()
        self._dtls_client_random.setPlaceholderText("client_random (hex, 64 chars)")
        dtls_layout.addRow("Client Random:", self._dtls_client_random)
        self._dtls_server_random = QLineEdit()
        self._dtls_server_random.setPlaceholderText("server_random (hex, 64 chars)")
        dtls_layout.addRow("Server Random:", self._dtls_server_random)
        self._dtls_suite = QComboBox()
        self._dtls_suite.addItems(list(CRYPTO_SUITES.keys()))
        self._dtls_suite.setCurrentText(DEFAULT_SUITE)
        dtls_layout.addRow("SRTP Suite:", self._dtls_suite)
        self._dtls_role = QComboBox()
        self._dtls_role.addItems(["from-client", "from-server", "unknown"])
        dtls_layout.addRow("Role:", self._dtls_role)
        self._dtls_add_btn = QPushButton("Derive & Add SRTP Key")
        self._dtls_add_btn.clicked.connect(self._add_dtls_key)
        dtls_layout.addRow(self._dtls_add_btn)
        srtp_layout.addWidget(dtls_group)

        right_tabs.addTab(srtp_widget, "SRTP Keys")

        # Tab 4: 日志
        log_widget = QWidget()
        log_layout = QVBoxLayout(log_widget)
        self._log_text = QTextEdit()
        self._log_text.setReadOnly(True)
        self._log_text.setStyleSheet("font-family: Consolas, monospace; font-size: 9pt;")
        log_layout.addWidget(self._log_text, 1)
        right_tabs.addTab(log_widget, "Logs")

        # 状态栏
        self._status = QStatusBar()
        self.setStatusBar(self._status)
        self._status.showMessage("Ready")

    # ---------- 事件处理 ----------

    def _on_port_changed(self) -> None:
        self._port_start = self._port_start_spin.value()
        self._port_end = self._port_end_spin.value()

    def _on_mos_changed(self) -> None:
        self._mos_threshold = self._mos_spin.value()
        if self._registry:
            self._registry._mos_threshold = self._mos_threshold

    def _toggle_monitoring(self) -> None:
        if self._running:
            self._stop_monitoring()
        else:
            self._start_monitoring()

    def _start_monitoring(self) -> None:
        if self._loop_thread is not None and self._loop_thread.is_alive():
            QMessageBox.warning(self, "Warning", "Already running")
            return

        self._start_btn.setEnabled(False)
        self._status.showMessage("Starting...")

        # 在独立线程中运行 asyncio 事件循环
        self._loop_thread = threading.Thread(target=self._run_event_loop, daemon=True)
        self._loop_thread.start()

    def _run_event_loop(self) -> None:
        try:
            self._loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self._loop)

            self._registry = StreamRegistry(
                mos_alert_threshold=self._mos_threshold,
                on_mos_alert=self._handle_mos_alert,
                on_content_alert=self._handle_content_alert,
                capture_manager=self._capture,
            )
            self._registry.set_ai_enabled(self._ai_checkbox.isChecked())
            self._capture = CaptureManager()

            # RTP 监听回调
            async def on_packet(pkt: RtpPacket, host: str, port: int) -> None:
                self._capture.feed_packet(pkt)
                await self._registry.feed_packet(pkt, host, port)

            self._listener = RtpListenerManager(
                port_range=range(self._port_start, self._port_end + 1),
                bind_addr="0.0.0.0",
                on_packet=on_packet,
                loop=self._loop,
                srtp_manager=self._srtp,
            )
            self._loop.run_until_complete(self._listener.start())

            # Dashboard
            self._dashboard = DashboardServer(self._registry, host="0.0.0.0", port=8080)
            self._loop.run_until_complete(self._dashboard.start())

            self._running = True
            self._loop.call_soon_threadsafe(self._update_ui_for_start)

            # 清理超时流的定时任务
            async def cleanup_loop():
                while self._running:
                    await asyncio.sleep(5.0)
                    self._registry.cleanup_stale()
                    self._srtp.cleanup_stale()

            self._loop.create_task(cleanup_loop())

            self._loop.run_forever()
        except Exception as exc:
            logger.exception("Event loop failed")
            self._loop.call_soon_threadsafe(
                lambda: QMessageBox.critical(self, "Error", str(exc))
            )

    def _update_ui_for_start(self) -> None:
        self._start_btn.setText("Stop Monitoring")
        self._start_btn.setEnabled(True)
        self._dashboard_btn.setEnabled(True)
        self._status.showMessage(
            f"Monitoring {self._port_start}-{self._port_end} | "
            f"SRTP contexts: {len(self._srtp.get_all_contexts())} | "
            f"Dashboard: http://localhost:8080"
        )

    def _stop_monitoring(self) -> None:
        if self._loop and self._loop.is_running():
            self._loop.call_soon_threadsafe(self._shutdown_loop)

    def _shutdown_loop(self) -> None:
        async def _shutdown():
            if self._listener:
                self._listener.stop()
            if self._dashboard:
                await self._dashboard.stop()
            self._running = False
            for task in asyncio.all_tasks(self._loop):
                task.cancel()
            self._loop.stop()

        asyncio.ensure_future(_shutdown(), loop=self._loop)

    def _open_dashboard(self) -> None:
        QDesktopServices.openUrl(QUrl("http://localhost:8080"))

    def _export_report(self) -> None:
        try:
            filepath = export_report(self._registry)
            QMessageBox.information(self, "Report Exported", f"Report saved to:\n{filepath}")
            self._log(f"PDF report exported: {filepath}")
        except Exception as exc:
            QMessageBox.critical(self, "Error", f"Report export failed:\n{exc}")

    def _handle_mos_alert(self, analyzer: StreamAnalyzer, snap: StreamMetrics) -> None:
        filepath = self._capture.on_mos_alert(analyzer, snap)
        if filepath:
            self._loop.call_soon_threadsafe(
                lambda: self._log(f"Quality alert: SSRC=0x{analyzer.info.ssrc:08X} MOS={snap.mos:.2f} saved: {filepath}")
            )

    def _handle_content_alert(self, analyzer: StreamAnalyzer,
                              content_buffer: ContentBuffer,
                              ai_result: ContentAnalysisResult) -> None:
        saved_files = self._capture.on_content_anomaly(analyzer, content_buffer, ai_result)
        if saved_files:
            files_str = ", ".join(saved_files)
            self._loop.call_soon_threadsafe(
                lambda: self._log(
                    f"Content anomaly: SSRC=0x{analyzer.info.ssrc:08X} "
                    f"Score={ai_result.anomaly_score:.1f} "
                    f"RootCause={ai_result.root_cause} "
                    f"saved: {files_str}"
                )
            )

    def _on_ai_toggled(self) -> None:
        enabled = self._ai_checkbox.isChecked()
        if self._registry:
            self._registry.set_ai_enabled(enabled)
        self._log(f"AI content analysis {'enabled' if enabled else 'disabled'}")

    def _on_stream_selected(self) -> None:
        rows = self._stream_table.selectedItems()
        if not rows:
            return
        row = rows[0].row()
        ssrc_text = self._stream_table.item(row, 0).text()
        try:
            ssrc = int(ssrc_text, 0)
        except ValueError:
            return
        analyzer = self._registry.get_stream(ssrc)
        if analyzer:
            self._update_detail(analyzer)
            self._update_ai_detail(ssrc)

    def _open_capture_file(self, item: QListWidgetItem) -> None:
        filepath = item.data(Qt.UserRole)
        if filepath:
            import os
            if sys.platform == "win32":
                os.startfile(filepath)  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                import subprocess
                subprocess.run(["open", filepath])
            else:
                import subprocess
                subprocess.run(["xdg-open", filepath])

    # ---------- SRTP 操作 ----------

    def _import_keylog_dialog(self) -> None:
        filepath, _ = QFileDialog.getOpenFileName(
            self, "Import Keylog File", "",
            "Keylog Files (*.log *.txt *.keylog);;All Files (*)"
        )
        if filepath:
            self._load_keylog(filepath)

    def _load_keylog(self, filepath: str) -> None:
        try:
            parser = KeylogParser.parse_file(filepath)
            count = parser.apply_to_manager(self._srtp)
            self._log(f"Imported {count} SRTP keys from keylog: {filepath}")
            self._refresh_srtp_table()
            # 如果监听器已在运行，更新其 SRTP 管理器
            if self._listener:
                self._listener.set_srtp_manager(self._srtp)
            QMessageBox.information(
                self, "Keys Imported",
                f"Successfully imported {count} SRTP key(s).\n\n"
                f"SRTP entries: {len(parser.srtp_entries)}\n"
                f"TLS entries: {len(parser.tls_entries)}\n"
                f"RFC5705 entries: {len(parser.rfc5705_entries)}"
            )
        except Exception as exc:
            QMessageBox.critical(self, "Import Failed", str(exc))
            logger.exception("Keylog import failed")

    def _add_key_manual(self) -> None:
        dialog = SrtpKeyDialog(self)
        if dialog.exec_() == QDialog.Accepted:
            params = dialog.get_params()
            if params:
                try:
                    self._srtp.add_context(
                        ssrc=params["ssrc"],
                        master_key=params["master_key"],
                        master_salt=params["master_salt"],
                        suite_name=params["suite"],
                        role=params["role"],
                        header_encrypted=params["header_encrypted"],
                    )
                    self._log(f"Manually added SRTP key: SSRC=0x{params['ssrc']:08X}")
                    self._refresh_srtp_table()
                    if self._listener:
                        self._listener.set_srtp_manager(self._srtp)
                except Exception as exc:
                    QMessageBox.critical(self, "Error", f"Failed to add key: {exc}")

    def _clear_srtp_keys(self) -> None:
        reply = QMessageBox.question(
            self, "Confirm",
            "Clear all SRTP keys? This cannot be undone.",
            QMessageBox.Yes | QMessageBox.No,
        )
        if reply == QMessageBox.Yes:
            self._srtp = SrtpManager()
            self._refresh_srtp_table()
            if self._listener:
                self._listener.set_srtp_manager(self._srtp)
            self._log("All SRTP keys cleared")

    def _add_dtls_key(self) -> None:
        try:
            ssrc = int(self._dtls_ssrc.text().strip(), 0)
        except ValueError:
            QMessageBox.warning(self, "Invalid SSRC", "Please enter a valid SSRC.")
            return

        try:
            master_secret = bytes.fromhex(self._dtls_master_secret.text().strip())
            client_random = bytes.fromhex(self._dtls_client_random.text().strip())
            server_random = bytes.fromhex(self._dtls_server_random.text().strip())
        except ValueError as exc:
            QMessageBox.warning(self, "Invalid Hex", f"Hex parse error: {exc}")
            return

        suite = self._dtls_suite.currentText()
        role = self._dtls_role.currentText()

        try:
            add_dtls_srtp_context(
                self._srtp, ssrc, master_secret, client_random, server_random,
                suite_name=suite, role=role,
            )
            self._log(f"DTLS-SRTP key derived: SSRC=0x{ssrc:08X} suite={suite}")
            self._refresh_srtp_table()
            if self._listener:
                self._listener.set_srtp_manager(self._srtp)
        except Exception as exc:
            QMessageBox.critical(self, "Error", f"Key derivation failed: {exc}")

    def _refresh_srtp_table(self) -> None:
        stats = self._srtp.get_stats()
        self._srtp_table.setRowCount(len(stats))
        for row, (ssrc, info) in enumerate(stats.items()):
            items = [
                f"0x{ssrc:08X}",
                info["role"],
                info["suite"],
                str(info["packets_decrypted"]),
                str(info["packets_auth_failed"]),
                str(info["packets_replay_dropped"]),
                "Yes" if info["header_encrypted"] else "No",
            ]
            for col, text in enumerate(items):
                self._srtp_table.setItem(row, col, QTableWidgetItem(text))

    # ---------- 刷新 ----------

    def _refresh_display(self) -> None:
        if not self._running:
            return
        streams = self._registry.get_all_streams()
        ai_results = self._registry.get_all_ai_results()
        self._stream_table.setRowCount(len(streams))
        for row, s in enumerate(streams):
            latest = s.get_latest_metrics()
            srtp_ctx = self._srtp.get_context(s.info.ssrc)
            srtp_label = f"{'Y' if srtp_ctx else 'N'} ({srtp_ctx.role if srtp_ctx else '-'})"

            ai = ai_results.get(s.info.ssrc)
            if ai and ai.is_anomaly:
                content_label = f"⚠️ {ai.anomaly_score:.0f}"
                content_color = "#ff4444"
            elif ai:
                content_label = f"✓ {ai.anomaly_score:.0f}"
                content_color = "#44aa44"
            else:
                content_label = "-"
                content_color = None

            items = [
                f"0x{s.info.ssrc:08X}",
                s.info.media_kind,
                s.info.codec,
                f"{s.info.source_ip}:{s.info.source_port}",
                str(s.info.total_packets),
                f"{s.info.total_lost / max(s.info.total_packets, 1):.2%}",
                f"{latest.mos:.2f}" if latest else "-",
                srtp_label,
                content_label,
            ]
            for col, text in enumerate(items):
                item = QTableWidgetItem(text)
                if col == 8 and content_color:
                    item.setForeground(Qt.red if content_color == "#ff4444" else Qt.darkGreen)
                    font = item.font()
                    font.setBold(True)
                    item.setFont(font)
                self._stream_table.setItem(row, col, item)

        # 保持当前选中流的详情更新
        selected = self._stream_table.selectedItems()
        if selected:
            row = selected[0].row()
            ssrc_text = self._stream_table.item(row, 0).text()
            try:
                ssrc = int(ssrc_text, 0)
                analyzer = self._registry.get_stream(ssrc)
                if analyzer:
                    self._update_detail(analyzer)
                    self._update_ai_detail(ssrc)
            except ValueError:
                pass

        # 更新 SRTP 状态表
        self._refresh_srtp_table()

        # 更新状态栏
        srtp_ctx_count = len(self._srtp.get_all_contexts())
        self._status.showMessage(
            f"Active streams: {len(streams)} | "
            f"SRTP contexts: {srtp_ctx_count} | "
            f"Ports: {len(self._listener.active_ports) if self._listener else 0} | "
            f"Dashboard: http://localhost:8080"
        )

    def _update_detail(self, analyzer: StreamAnalyzer) -> None:
        summary = analyzer.get_stream_summary()
        srtp_ctx = self._srtp.get_context(analyzer.info.ssrc)
        srtp_info = ""
        if srtp_ctx:
            srtp_info = f" | SRTP: {srtp_ctx.suite.name} ({srtp_ctx.role})"
            srtp_info += f" | Decrypted: {srtp_ctx.packets_decrypted}"
            if srtp_ctx.packets_auth_failed:
                srtp_info += f" | AuthFail: {srtp_ctx.packets_auth_failed}"

        if summary:
            self._detail_label.setText(
                f"SSRC: {summary.get('ssrc', '')} | {summary.get('media_kind', '')} | "
                f"{summary.get('codec', '')} | {summary.get('source', '')} | "
                f"Resolution: {summary.get('resolution', 'N/A')}"
                f"{srtp_info}"
            )
        snaps = analyzer.get_window_metrics()
        self._detail_table.setRowCount(len(snaps))
        for row, snap in enumerate(snaps):
            items = [
                time.strftime("%H:%M:%S", time.localtime(snap.timestamp)),
                str(snap.packet_count),
                str(snap.lost_packets),
                f"{snap.loss_rate:.2%}",
                f"{snap.jitter:.2f}",
                f"{snap.delay:.2f}",
                f"{snap.mos:.2f}",
                f"{snap.fps:.1f}",
                f"{snap.bitrate:.1f}",
            ]
            for col, text in enumerate(items):
                self._detail_table.setItem(row, col, QTableWidgetItem(text))

    def _update_ai_detail(self, ssrc: int) -> None:
        ai_result = self._registry.get_latest_ai_result(ssrc)
        if ai_result is None:
            self._ai_detail_label.setText("No AI analysis data available yet")
            self._ai_table.setRowCount(0)
            return

        status = "⚠️ ANOMALY" if ai_result.is_anomaly else "✓ Normal"
        status_color = "#ff4444" if ai_result.is_anomaly else "#44aa44"
        self._ai_detail_label.setText(
            f"SSRC: 0x{ssrc:08X} | AI Score: {ai_result.anomaly_score:.1f}/{ai_result.threshold:.0f} | "
            f"Status: <span style='color:{status_color};font-weight:bold;'>{status}</span>"
        )
        self._ai_detail_label.setTextFormat(Qt.RichText)

        rows = []
        rows.append(("Timestamp", time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ai_result.timestamp))))
        rows.append(("Anomaly Score", f"{ai_result.anomaly_score:.1f}"))
        rows.append(("Threshold", f"{ai_result.threshold:.0f}"))
        rows.append(("Is Anomaly", "Yes" if ai_result.is_anomaly else "No"))

        if ai_result.audio_anomalies:
            rows.append(("Audio Anomalies", ", ".join(ai_result.audio_anomalies)))
        if ai_result.video_anomalies:
            rows.append(("Video Anomalies", ", ".join(ai_result.video_anomalies)))

        rows.append(("Root Cause", ai_result.root_cause))
        rows.append(("Root Cause Confidence", f"{ai_result.root_cause_confidence:.1%}"))

        rows.append(("--- RTP Metrics ---", ""))
        rows.append(("Loss Rate", f"{ai_result.rtp_loss_rate:.2%}" if ai_result.rtp_loss_rate is not None else "N/A"))
        rows.append(("Jitter (ms)", f"{ai_result.rtp_jitter:.2f}" if ai_result.rtp_jitter is not None else "N/A"))
        rows.append(("MOS", f"{ai_result.rtp_mos:.2f}" if ai_result.rtp_mos is not None else "N/A"))

        if ai_result.audio_scores:
            rows.append(("--- Audio Scores ---", ""))
            for k, v in ai_result.audio_scores.items():
                rows.append((f"  {k}", f"{v:.1f}"))

        if ai_result.video_scores:
            rows.append(("--- Video Scores ---", ""))
            for k, v in ai_result.video_scores.items():
                rows.append((f"  {k}", f"{v:.1f}"))

        self._ai_table.setRowCount(len(rows))
        for row, (key, value) in enumerate(rows):
            key_item = QTableWidgetItem(key)
            if key.startswith("---"):
                font = key_item.font()
                font.setBold(True)
                key_item.setFont(font)
            self._ai_table.setItem(row, 0, key_item)
            self._ai_table.setItem(row, 1, QTableWidgetItem(value))

    def _refresh_capture_list(self) -> None:
        self._cap_list.clear()
        for directory, prefix in [("screenshots", "IMG: "), ("recordings", "AUD: ")]:
            path = Path(directory)
            if path.exists():
                for f in sorted(path.iterdir(), key=lambda x: x.stat().st_mtime, reverse=True)[:50]:
                    item = QListWidgetItem(prefix + f.name)
                    item.setData(Qt.UserRole, str(f.absolute()))
                    self._cap_list.addItem(item)

    def _log(self, msg: str) -> None:
        timestamp = time.strftime("%H:%M:%S", time.localtime())
        self._log_text.append(f"[{timestamp}] {msg}")
