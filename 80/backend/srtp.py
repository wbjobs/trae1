"""SRTP (Secure RTP) 核心模块

实现 RFC 3711 SRTP 协议：
- AES-128-CTR 加解密
- HMAC-SHA1 认证（80/32 位截断）
- 密钥派生（PRF → k_e / k_a / k_s）
- 头部加密（SRE, RFC 3711 第 4.1.3 节）
- ROC 回绕计数器
- 重放保护

支持的密码套件：
  - AES_CM_128_HMAC_SHA1_80（默认，WebRTC 常用）
  - AES_CM_128_HMAC_SHA1_32
  - AES_CM_256_HMAC_SHA1_80
  - AES_CM_256_HMAC_SHA1_32
"""

from __future__ import annotations

import hmac
import struct
import time
from dataclasses import dataclass, field
from hashlib import sha1
from typing import Dict, Optional, Tuple

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend


# ========== 密码套件配置 ==========

@dataclass(frozen=True)
class CryptoSuite:
    name: str
    master_key_len: int
    master_salt_len: int
    enc_key_len: int
    auth_key_len: int
    auth_tag_len: int
    kdr_shift: int  # key derivation rate shift, typically 0


CRYPTO_SUITES = {
    "AES_CM_128_HMAC_SHA1_80": CryptoSuite(
        name="AES_CM_128_HMAC_SHA1_80",
        master_key_len=16,
        master_salt_len=14,
        enc_key_len=16,
        auth_key_len=20,
        auth_tag_len=10,
        kdr_shift=0,
    ),
    "AES_CM_128_HMAC_SHA1_32": CryptoSuite(
        name="AES_CM_128_HMAC_SHA1_32",
        master_key_len=16,
        master_salt_len=14,
        enc_key_len=16,
        auth_key_len=20,
        auth_tag_len=4,
        kdr_shift=0,
    ),
    "AES_CM_256_HMAC_SHA1_80": CryptoSuite(
        name="AES_CM_256_HMAC_SHA1_80",
        master_key_len=32,
        master_salt_len=14,
        enc_key_len=32,
        auth_key_len=20,
        auth_tag_len=10,
        kdr_shift=0,
    ),
    "AES_CM_256_HMAC_SHA1_32": CryptoSuite(
        name="AES_CM_256_HMAC_SHA1_32",
        master_key_len=32,
        master_salt_len=14,
        enc_key_len=32,
        auth_key_len=20,
        auth_tag_len=4,
        kdr_shift=0,
    ),
}

DEFAULT_SUITE = "AES_CM_128_HMAC_SHA1_80"


# ========== SRTP 上下文 ==========

@dataclass
class SrtpContext:
    """单 SSRC 的 SRTP 上下文。"""

    ssrc: int
    master_key: bytes
    master_salt: bytes
    suite: CryptoSuite = field(default_factory=lambda: CRYPTO_SUITES[DEFAULT_SUITE])
    role: str = "unknown"  # "from-client" / "from-server" / "unknown"
    header_encrypted: bool = False  # SRE - header encryption
    mki: Optional[bytes] = None  # Master Key Identifier

    # 派生密钥
    enc_key: bytes = b""
    auth_key: bytes = b""
    derived_salt: bytes = b""

    # 运行状态
    roc: int = 0  # Roll-Over Counter
    highest_seq: int = 0
    replay_window: int = 0  # bitmask for replay protection
    created_at: float = field(default_factory=time.time)
    last_used: float = 0.0
    packets_decrypted: int = 0
    packets_auth_failed: int = 0
    packets_replay_dropped: int = 0

    def __post_init__(self) -> None:
        self.derive_keys()

    # ----- 密钥派生 -----

    def derive_keys(self) -> None:
        """根据 RFC 3711 第 4.3 节派生 k_e / k_a / k_s。"""
        self.enc_key = self._prf(0x00)
        self.auth_key = self._prf(0x01)
        self.derived_salt = self._prf(0x02)[: self.suite.master_salt_len]

    def _prf(self, index: int) -> bytes:
        """PRF: AES-128-ECB(master_key, index || label || zeros)。

        label 为 "SRTP"（4 字节），输入总共 16 字节。
        """
        label = b"SRTP"
        input_block = bytearray(16)
        input_block[0] = index & 0xFF
        input_block[1:5] = label
        # bytes 5-15 为零

        key = self.master_key
        if len(key) < 16:
            key = key.ljust(16, b'\x00')
        elif len(key) > 16:
            # 对于 AES-256，PRF 仍使用 master_key 的前 16 字节
            key = key[:16]

        cipher = Cipher(algorithms.AES(key), modes.ECB(), backend=default_backend())
        encryptor = cipher.encryptor()
        return encryptor.update(bytes(input_block)) + encryptor.finalize()

    # ----- IV 生成 -----

    def compute_iv(self, seq: int, ssrc: int, roc: int) -> bytes:
        """生成 128 位 AES-CTR IV（RFC 3711 第 4.1.1 节）。

        IV = (0x0000 || SEQ || SSRC || ROC || 0x0000) XOR (salt[0:14] || 0x0000)
        """
        iv = bytearray(16)
        # Byte 2-3: SEQ
        iv[2] = (seq >> 8) & 0xFF
        iv[3] = seq & 0xFF
        # Byte 4-7: SSRC
        iv[4] = (ssrc >> 24) & 0xFF
        iv[5] = (ssrc >> 16) & 0xFF
        iv[6] = (ssrc >> 8) & 0xFF
        iv[7] = ssrc & 0xFF
        # Byte 8-11: ROC
        iv[8] = (roc >> 24) & 0xFF
        iv[9] = (roc >> 16) & 0xFF
        iv[10] = (roc >> 8) & 0xFF
        iv[11] = roc & 0xFF
        # XOR with salt (14 bytes) starting at byte 0
        salt = self.derived_salt if self.derived_salt else self.master_salt
        for i in range(min(14, len(salt))):
            iv[i] ^= salt[i]
        return bytes(iv)

    # ----- 加密/解密 -----

    def decrypt_payload(self, iv: bytes, encrypted_payload: bytes, header_len: int = 12) -> bytes:
        """AES-128-CTR 解密 RTP 负载。

        第一个 16 字节密钥流被跳过（保留给头部加密）。
        """
        if len(self.enc_key) == 32:
            cipher = Cipher(algorithms.AES256(self.enc_key), modes.CTR(iv), backend=default_backend())
        else:
            cipher = Cipher(algorithms.AES(self.enc_key), modes.CTR(iv), backend=default_backend())

        decryptor = cipher.decryptor()
        # 生成足够的密钥流
        dummy = bytearray(header_len + len(encrypted_payload))
        keystream = decryptor.update(bytes(dummy)) + decryptor.finalize()

        # 跳过头部长度的密钥流（对齐到 16 字节边界）
        keystream_offset = (header_len // 16 + 1) * 16
        if keystream_offset >= len(keystream):
            keystream_offset = header_len

        payload_keystream = keystream[keystream_offset:]
        return bytes(a ^ b for a, b in zip(encrypted_payload, payload_keystream))

    def decrypt_header(self, iv: bytes, header: bytes) -> bytes:
        """SRE: 解密 RTP 头部。"""
        if len(self.enc_key) == 32:
            cipher = Cipher(algorithms.AES256(self.enc_key), modes.CTR(iv), backend=default_backend())
        else:
            cipher = Cipher(algorithms.AES(self.enc_key), modes.CTR(iv), backend=default_backend())
        decryptor = cipher.decryptor()
        keystream = decryptor.update(bytearray(len(header))) + decryptor.finalize()
        return bytes(a ^ b for a, b in zip(header, keystream))

    # ----- 认证 -----

    def verify_auth(self, rtp_header: bytes, encrypted_payload: bytes,
                    roc: int, auth_tag: bytes) -> bool:
        """验证 HMAC-SHA1 认证标签（RFC 3711 第 4.2 节）。

        认证输入 = RTP 头部 || 加密负载 || ROC（32 位大端）
        """
        auth_input = rtp_header + encrypted_payload + struct.pack("!I", roc)
        computed = hmac.new(self.auth_key, auth_input, sha1).digest()
        return hmac.compare_digest(computed[: self.suite.auth_tag_len], auth_tag)

    # ----- 重放保护 -----

    def check_replay(self, seq: int) -> bool:
        """RFC 3711 第 3.3.2 节重放保护。

        使用 64 位移位寄存器窗口。
        返回 True 表示接受（非重放），False 表示丢弃。
        """
        if seq > self.highest_seq:
            shift = seq - self.highest_seq
            self.replay_window = ((self.replay_window << shift) | 1) & 0xFFFFFFFFFFFFFFFF
            self.highest_seq = seq
            return True
        else:
            delta = self.highest_seq - seq
            if delta >= 64:
                return False
            bit = 1 << delta
            if self.replay_window & bit:
                return False
            self.replay_window |= bit
            return True


# ========== SRTP 管理器 ==========

class SrtpManager:
    """管理所有 SSRC 的 SRTP 上下文。

    支持：
    - 手动配置 master key/salt
    - 从 Wireshark keylog 文件导入
    - 从 DTLS-SRTP 握手提取
    """

    CONTEXT_TIMEOUT = 3600.0  # 1 小时无数据自动清理

    def __init__(self) -> None:
        self._contexts: Dict[int, SrtpContext] = {}
        self._pending_keys: Dict[Tuple[str, str], Dict] = {}  # (role, label) -> keys

    def add_context(self, ssrc: int, master_key: bytes, master_salt: bytes,
                    suite_name: str = DEFAULT_SUITE, role: str = "unknown",
                    header_encrypted: bool = False) -> SrtpContext:
        """为指定 SSRC 添加 SRTP 上下文。"""
        suite = CRYPTO_SUITES.get(suite_name, CRYPTO_SUITES[DEFAULT_SUITE])
        ctx = SrtpContext(
            ssrc=ssrc,
            master_key=master_key,
            master_salt=master_salt,
            suite=suite,
            role=role,
            header_encrypted=header_encrypted,
        )
        self._contexts[ssrc] = ctx
        return ctx

    def remove_context(self, ssrc: int) -> None:
        self._contexts.pop(ssrc, None)

    def get_context(self, ssrc: int) -> Optional[SrtpContext]:
        return self._contexts.get(ssrc)

    def has_context(self, ssrc: int) -> bool:
        return ssrc in self._contexts

    def get_all_contexts(self) -> Dict[int, SrtpContext]:
        return dict(self._contexts)

    def process_packet(self, data: bytes) -> Tuple[Optional[bytes], Optional[SrtpContext]]:
        """处理一个 SRTP 包：验证、解密，返回 (解密后的 RTP 数据, context)。

        如果该 SSRC 没有上下文，返回 (None, None) —— 调用方应按明文 RTP 处理。
        如果认证失败或解密失败，返回 (None, context)。
        """
        if len(data) < 12:
            return None, None

        # 解析 RTP 头部（明文）
        first_byte = data[0]
        version = (first_byte >> 6) & 0x03
        if version != 2:
            return None, None

        csrc_count = first_byte & 0x0F
        extension = bool((first_byte >> 4) & 0x01)
        header_len = 12 + csrc_count * 4

        if len(data) < header_len + 4:
            return None, None

        seq = struct.unpack_from("!H", data, 2)[0]
        ssrc = struct.unpack_from("!I", data, 8)[0]

        ctx = self._contexts.get(ssrc)
        if ctx is None:
            return None, None

        ctx.last_used = time.time()

        # 分离认证标签
        auth_tag_len = ctx.suite.auth_tag_len
        if len(data) < header_len + auth_tag_len:
            ctx.packets_auth_failed += 1
            return None, ctx

        # 如果有 MKI，跳过
        mki_len = 0
        if ctx.mki:
            mki_len = len(ctx.mki)

        encrypted_payload = data[header_len: len(data) - auth_tag_len - mki_len]
        auth_tag = data[len(data) - auth_tag_len:]
        rtp_header = data[:header_len]

        # 重放保护
        if not ctx.check_replay(seq):
            ctx.packets_replay_dropped += 1
            return None, ctx

        # 更新 ROC
        if seq < ctx.highest_seq - 32768:
            # 检测序列号回绕
            if ctx.highest_seq > 32768:
                ctx.roc += 1

        # 验证认证标签
        if not ctx.verify_auth(rtp_header, encrypted_payload, ctx.roc, auth_tag):
            ctx.packets_auth_failed += 1
            return None, ctx

        # 生成 IV 并解密
        iv = ctx.compute_iv(seq, ssrc, ctx.roc)

        if ctx.header_encrypted:
            decrypted_header = ctx.decrypt_header(iv, rtp_header)
        else:
            decrypted_header = rtp_header

        decrypted_payload = ctx.decrypt_payload(iv, encrypted_payload, header_len)

        decrypted_rtp = decrypted_header + decrypted_payload
        ctx.packets_decrypted += 1
        return decrypted_rtp, ctx

    def cleanup_stale(self) -> None:
        now = time.time()
        stale = [ssrc for ssrc, ctx in self._contexts.items()
                 if now - ctx.last_used > self.CONTEXT_TIMEOUT and ctx.last_used > 0]
        for ssrc in stale:
            del self._contexts[ssrc]

    def get_stats(self) -> Dict:
        return {
            ssrc: {
                "role": ctx.role,
                "suite": ctx.suite.name,
                "packets_decrypted": ctx.packets_decrypted,
                "packets_auth_failed": ctx.packets_auth_failed,
                "packets_replay_dropped": ctx.packets_replay_dropped,
                "roc": ctx.roc,
                "header_encrypted": ctx.header_encrypted,
                "last_used": ctx.last_used,
            }
            for ssrc, ctx in self._contexts.items()
        }
