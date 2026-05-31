"""Wireshark 密钥日志文件解析器

支持的格式：

1. SRTP_MASTER_KEY（Wireshark SRTP 专用）
   SRTP_MASTER_KEY "from-client" 0x<SSRC> <master_key_hex> <master_salt_hex>
   SRTP_MASTER_KEY "from-server" 0x<SSRC> <master_key_hex> <master_salt_hex>

2. CLIENT_RANDOM（TLS/DTLS 密钥日志，用于 DTLS-SRTP）
   CLIENT_RANDOM <client_random_hex> <master_secret_hex>

3. RFC5705_KEYING_MATERIAL_EXPORTER（TLS Exporter 格式）
   RFC5705_KEYING_MATERIAL_EXPORTER <label> <client_random> <server_random> <exporter_data>

4. 通用的 SRTP 密钥配置行
   # 注释行（以 # 开头）
   SRTP <role> <ssrc_hex> <master_key_hex> <master_salt_hex> [suite]
"""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from .srtp import SrtpManager, CRYPTO_SUITES, DEFAULT_SUITE

logger = logging.getLogger(__name__)


@dataclass
class SrtpKeyEntry:
    """SRTP 密钥条目。"""

    role: str  # "from-client" / "from-server" / "unknown"
    ssrc: int
    master_key: bytes
    master_salt: bytes
    suite: str = DEFAULT_SUITE
    header_encrypted: bool = False


@dataclass
class TlsKeyEntry:
    """TLS/DTLS 密钥条目（用于 DTLS-SRTP 派生）。"""

    client_random: bytes
    master_secret: bytes
    cipher_suite: str = ""


class KeylogParser:
    """Wireshark 密钥日志文件解析器。"""

    # SRTP_MASTER_KEY "from-client" 0x12345678 aabb...ddeeff aabb...ddeeff
    _SRTP_MASTER_KEY_RE = re.compile(
        r'^SRTP_MASTER_KEY\s+"([^"]+)"\s+(?:0x)?([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)'
        r'(?:\s+(AES_CM_\w+))?'
        r'(?:\s+(header_enc))?'
    )

    # SRTP from-client 0x12345678 aabb...ddeeff aabb...ddeeff
    _SRTP_GENERIC_RE = re.compile(
        r'^SRTP\s+(from-client|from-server|unknown)\s+(?:0x)?([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)'
        r'(?:\s+(AES_CM_\w+))?'
        r'(?:\s+(header_enc))?'
    )

    # CLIENT_RANDOM <32 bytes hex> <48 bytes hex>
    _CLIENT_RANDOM_RE = re.compile(
        r'^CLIENT_RANDOM\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)'
    )

    # RFC5705_KEYING_MATERIAL_EXPORTER <label> <client_random> <server_random> <exporter_data>
    _RFC5705_RE = re.compile(
        r'^RFC5705_KEYING_MATERIAL_EXPORTER\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)'
    )

    def __init__(self) -> None:
        self.srtp_entries: List[SrtpKeyEntry] = []
        self.tls_entries: List[TlsKeyEntry] = []
        self.rfc5705_entries: List[Dict] = []

    @classmethod
    def parse_file(cls, filepath: str) -> "KeylogParser":
        """解析一个 keylog 文件。"""
        parser = cls()
        path = Path(filepath)
        if not path.exists():
            raise FileNotFoundError(f"Keylog file not found: {filepath}")

        with open(path, "r", encoding="utf-8") as f:
            for line_no, line in enumerate(f, 1):
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parser._parse_line(line, line_no)

        logger.info(
            "Parsed keylog: %d SRTP keys, %d TLS keys, %d RFC5705 entries",
            len(parser.srtp_entries), len(parser.tls_entries), len(parser.rfc5705_entries),
        )
        return parser

    def _parse_line(self, line: str, line_no: int) -> None:
        # 尝试 SRTP_MASTER_KEY
        m = self._SRTP_MASTER_KEY_RE.match(line)
        if m:
            try:
                role = m.group(1)
                ssrc = int(m.group(2), 16)
                master_key = bytes.fromhex(m.group(3))
                master_salt = bytes.fromhex(m.group(4))
                suite = m.group(5) or DEFAULT_SUITE
                header_enc = bool(m.group(6))
                self.srtp_entries.append(SrtpKeyEntry(
                    role=role, ssrc=ssrc,
                    master_key=master_key, master_salt=master_salt,
                    suite=suite, header_encrypted=header_enc,
                ))
            except (ValueError, IndexError) as exc:
                logger.warning("Line %d: Invalid SRTP_MASTER_KEY: %s", line_no, exc)
            return

        # 尝试通用 SRTP
        m = self._SRTP_GENERIC_RE.match(line)
        if m:
            try:
                role = m.group(1)
                ssrc = int(m.group(2), 16)
                master_key = bytes.fromhex(m.group(3))
                master_salt = bytes.fromhex(m.group(4))
                suite = m.group(5) or DEFAULT_SUITE
                header_enc = bool(m.group(6))
                self.srtp_entries.append(SrtpKeyEntry(
                    role=role, ssrc=ssrc,
                    master_key=master_key, master_salt=master_salt,
                    suite=suite, header_encrypted=header_enc,
                ))
            except (ValueError, IndexError) as exc:
                logger.warning("Line %d: Invalid SRTP entry: %s", line_no, exc)
            return

        # 尝试 CLIENT_RANDOM
        m = self._CLIENT_RANDOM_RE.match(line)
        if m:
            try:
                client_random = bytes.fromhex(m.group(1))
                master_secret = bytes.fromhex(m.group(2))
                self.tls_entries.append(TlsKeyEntry(
                    client_random=client_random,
                    master_secret=master_secret,
                ))
            except (ValueError, IndexError) as exc:
                logger.warning("Line %d: Invalid CLIENT_RANDOM: %s", line_no, exc)
            return

        # 尝试 RFC5705
        m = self._RFC5705_RE.match(line)
        if m:
            try:
                self.rfc5705_entries.append({
                    "label": m.group(1),
                    "client_random": bytes.fromhex(m.group(2)),
                    "server_random": bytes.fromhex(m.group(3)),
                    "exporter_data": bytes.fromhex(m.group(4)),
                })
            except (ValueError, IndexError) as exc:
                logger.warning("Line %d: Invalid RFC5705 entry: %s", line_no, exc)
            return

        logger.debug("Line %d: Unrecognized format, skipping", line_no)

    def apply_to_manager(self, manager: SrtpManager) -> int:
        """将解析出的 SRTP 密钥应用到 SrtpManager。

        返回成功添加的上下文数量。
        """
        count = 0
        for entry in self.srtp_entries:
            if entry.suite not in CRYPTO_SUITES:
                logger.warning("Unknown crypto suite: %s, using default", entry.suite)
                suite = DEFAULT_SUITE
            else:
                suite = entry.suite
            manager.add_context(
                ssrc=entry.ssrc,
                master_key=entry.master_key,
                master_salt=entry.master_salt,
                suite_name=suite,
                role=entry.role,
                header_encrypted=entry.header_encrypted,
            )
            count += 1
            logger.info(
                "Added SRTP context: SSRC=0x%08X role=%s suite=%s",
                entry.ssrc, entry.role, suite,
            )
        return count
