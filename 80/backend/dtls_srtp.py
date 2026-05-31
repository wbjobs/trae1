"""DTLS-SRTP 密钥提取模块

支持两种模式：
1. 从已知 DTLS master_secret + random 导出 SRTP 密钥（TLS Exporter, RFC 5705/5764）
2. 解析 DTLS 握手包（ClientHello/ServerHello）提取 random 值

TLS Exporter for DTLS-SRTP (RFC 5764):
  label = "EXTRACTOR-dtls_srtp"
  context = client_random || server_random
  keying_material = TLS-PRF(master_secret, label, context)
  master_key = keying_material[0:16]
  master_salt = keying_material[16:30]
"""

from __future__ import annotations

import hmac
import logging
import struct
from dataclasses import dataclass
from hashlib import sha256, sha1
from typing import Optional, Tuple

from .srtp import SrtpManager, CRYPTO_SUITES, DEFAULT_SUITE

logger = logging.getLogger(__name__)


# ========== DTLS 握手解析 ==========

@dataclass
class DtlsHandshakeInfo:
    client_random: bytes = b""
    server_random: bytes = b""
    cipher_suite: int = 0
    cipher_suite_name: str = ""
    srtp_profile: int = 0  # use_srtp extension profile


class DtlsHandshakeParser:
    """极简 DTLS 握手包解析器，仅提取 random 和 cipher_suite。"""

    # Content Type: 22 = Handshake
    CONTENT_HANDSHAKE = 22
    # Handshake Type: 1 = ClientHello, 2 = ServerHello
    HS_CLIENT_HELLO = 1
    HS_SERVER_HELLO = 2

    # DTLS Header: ContentType(1) + Version(2) + Epoch(2) + Seq(6) + Length(2) = 13 bytes
    DTLS_HEADER_LEN = 13

    @classmethod
    def parse_client_hello(cls, data: bytes) -> Optional[DtlsHandshakeInfo]:
        """从 DTLS 包中解析 ClientHello，提取 client_random。"""
        hs_body = cls._extract_handshake(data, cls.HS_CLIENT_HELLO)
        if hs_body is None or len(hs_body) < 34:
            return None

        info = DtlsHandshakeInfo()
        # client_version(2) + client_random(32) = 34
        info.client_random = hs_body[2:34]
        return info

    @classmethod
    def parse_server_hello(cls, data: bytes) -> Optional[DtlsHandshakeInfo]:
        """从 DTLS 包中解析 ServerHello，提取 server_random, cipher_suite。"""
        hs_body = cls._extract_handshake(data, cls.HS_SERVER_HELLO)
        if hs_body is None or len(hs_body) < 38:
            return None

        info = DtlsHandshakeInfo()
        # server_version(2) + server_random(32) = 34
        info.server_random = hs_body[2:34]

        # session_id_length(1) + session_id + cipher_suite(2)
        offset = 34
        session_id_len = hs_body[offset]
        offset += 1 + session_id_len
        if offset + 2 <= len(hs_body):
            info.cipher_suite = struct.unpack("!H", hs_body, offset)[0]
            info.cipher_suite_name = cls._cipher_suite_name(info.cipher_suite)
        return info

    @classmethod
    def _extract_handshake(cls, data: bytes, expected_type: int) -> Optional[bytes]:
        """从 DTLS 记录中提取握手消息体。"""
        if len(data) < cls.DTLS_HEADER_LEN:
            return None

        content_type = data[0]
        if content_type != cls.CONTENT_HANDSHAKE:
            return None

        # DTLS header: skip 13 bytes
        hs_payload = data[cls.DTLS_HEADER_LEN:]
        if len(hs_payload) < 4:
            return None

        # Handshake header: Type(1) + Length(3) + Seq(2) + FragmentOffset(3) + FragmentLength(3) = 12
        hs_type = hs_payload[0]
        if hs_type != expected_type:
            return None

        # Extract the handshake body (after the 12-byte handshake header)
        if len(hs_payload) < 12:
            return None
        return hs_payload[12:]

    @staticmethod
    def _cipher_suite_name(cs: int) -> str:
        names = {
            0x0035: "TLS_RSA_WITH_AES_256_CBC_SHA",
            0x003C: "TLS_RSA_WITH_AES_128_CBC_SHA256",
            0x003D: "TLS_RSA_WITH_AES_256_CBC_SHA256",
            0x008C: "TLS_PSK_WITH_AES_128_CBC_SHA256",
            0x008D: "TLS_PSK_WITH_AES_256_CBC_SHA384",
            0x009C: "TLS_RSA_WITH_AES_128_GCM_SHA256",
            0x009D: "TLS_RSA_WITH_AES_256_GCM_SHA384",
            0xC013: "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA",
            0xC014: "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA",
            0xC027: "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256",
            0xC028: "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384",
            0xC02F: "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256",
            0xC030: "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384",
            0xC02B: "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256",
            0xC02C: "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384",
            0xC0A8: "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256",
            0xC02B: "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256",
            0xCCA8: "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256",
            0xCCA9: "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256",
        }
        return names.get(cs, f"UNKNOWN_0x{cs:04X}")


# ========== TLS 1.2 PRF ==========

def _p_hash(secret: bytes, seed: bytes, length: int, hash_func) -> bytes:
    """TLS 1.2 P_hash 函数。

    A(0) = seed
    A(i) = HMAC(secret, A(i-1))
    P_hash = HMAC(secret, A(1) + seed) + HMAC(secret, A(2) + seed) + ...
    """
    result = bytearray()
    a = seed  # A(0)
    while len(result) < length:
        a = hmac.new(secret, a, hash_func).digest()
        result.extend(hmac.new(secret, a + seed, hash_func).digest())
    return bytes(result[:length])


def tls_prf_sha256(secret: bytes, label: str, seed: bytes, length: int) -> bytes:
    """TLS 1.2 PRF 使用 SHA-256。"""
    label_bytes = label.encode("ascii")
    return _p_hash(secret, label_bytes + seed, length, sha256)


def tls_prf_sha1(secret: bytes, label: str, seed: bytes, length: int) -> bytes:
    """TLS 1.0/1.1 PRF 使用 MD5+SHA1 组合。"""
    label_bytes = label.encode("ascii")
    combined_seed = label_bytes + seed

    # 拆分为两半
    half_len = (len(secret) + 1) // 2
    s1 = secret[:half_len]
    s2 = secret[half_len:]

    p_md5 = _p_hash(s1, combined_seed, length, __import__("hashlib").md5)
    p_sha1 = _p_hash(s2, combined_seed, length, sha1)
    return bytes(a ^ b for a, b in zip(p_md5, p_sha1))


# ========== SRTP 密钥导出 ==========

def derive_srtp_keys_from_dtls(
    master_secret: bytes,
    client_random: bytes,
    server_random: bytes,
    suite_name: str = DEFAULT_SUITE,
    tls_version: str = "1.2",
) -> Tuple[bytes, bytes]:
    """从 DTLS master_secret 导出 SRTP master_key 和 master_salt。

    使用 RFC 5764 的 TLS Exporter：
      keying_material = PRF(master_secret, "EXTRACTOR-dtls_srtp", client_random + server_random)

    返回 (master_key, master_salt)。
    """
    suite = CRYPTO_SUITES.get(suite_name, CRYPTO_SUITES[DEFAULT_SUITE])
    label = "EXTRACTOR-dtls_srtp"
    context = client_random + server_random
    needed = suite.master_key_len + suite.master_salt_len

    if tls_version >= "1.2":
        keying = tls_prf_sha256(master_secret, label, context, needed)
    else:
        keying = tls_prf_sha1(master_secret, label, context, needed)

    master_key = keying[: suite.master_key_len]
    master_salt = keying[suite.master_key_len : suite.master_key_len + suite.master_salt_len]
    return master_key, master_salt


def add_dtls_srtp_context(
    manager: SrtpManager,
    ssrc: int,
    master_secret: bytes,
    client_random: bytes,
    server_random: bytes,
    suite_name: str = DEFAULT_SUITE,
    role: str = "unknown",
    tls_version: str = "1.2",
) -> None:
    """从 DTLS 参数导出 SRTP 密钥并添加到管理器。"""
    master_key, master_salt = derive_srtp_keys_from_dtls(
        master_secret, client_random, server_random, suite_name, tls_version
    )
    manager.add_context(
        ssrc=ssrc,
        master_key=master_key,
        master_salt=master_salt,
        suite_name=suite_name,
        role=role,
    )
    logger.info(
        "DTLS-SRTP context added: SSRC=0x%08X role=%s suite=%s",
        ssrc, role, suite_name,
    )


# ========== 便捷函数 ==========

def extract_srtp_from_keylog_entry(
    manager: SrtpManager,
    client_random_hex: str,
    master_secret_hex: str,
    server_random_hex: str,
    ssrc: int,
    suite_name: str = DEFAULT_SUITE,
    role: str = "unknown",
    tls_version: str = "1.2",
) -> bool:
    """从 keylog 条目中提取 SRTP 密钥（需要已知 server_random）。

    适用于 Wireshark CLIENT_RANDOM 条目。
    """
    try:
        client_random = bytes.fromhex(client_random_hex)
        master_secret = bytes.fromhex(master_secret_hex)
        server_random = bytes.fromhex(server_random_hex)
    except ValueError:
        logger.error("Invalid hex in keylog entry")
        return False

    add_dtls_srtp_context(
        manager, ssrc, master_secret, client_random, server_random,
        suite_name, role, tls_version,
    )
    return True
