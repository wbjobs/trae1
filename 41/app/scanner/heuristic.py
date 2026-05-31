import math
import re
import struct
import os
from typing import List, Dict, Optional, Tuple
from dataclasses import dataclass
from collections import Counter


@dataclass
class HeuristicFinding:
    type: str
    name: str
    description: str
    severity: str
    offset: Optional[int] = None
    details: Optional[str] = None


class HeuristicScanner:
    ENTROPY_THRESHOLD_HIGH = 7.5
    ENTROPY_THRESHOLD_MEDIUM = 7.0

    MALWARE_SIGNATURES = {
        "backdoor_cmdshell": [
            b"cmd.exe /c",
            b"cmd.exe /r",
            b"cmd.exe /k",
            b"/bin/sh -c",
            b"/bin/bash -c",
        ],
        "reverse_shell": [
            b"bash -i >&",
            b"nc -e /bin/sh",
            b"python -c 'import socket",
            b"perl -e 'use Socket",
            b"php -r '$sock=fsockopen",
        ],
        "download_execute": [
            b"URLDownloadToFile",
            b"DownloadFile",
            b"Invoke-WebRequest",
            b"System.Net.WebClient",
            b"curl http",
            b"wget http",
        ],
        "credential_theft": [
            b"mimikatz",
            b"pwdump",
            b"gsecdump",
            b"fgdump",
            b"Windows Credentials Editor",
        ],
        "persistence": [
            b"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            b"schtasks /create",
            b"sc create",
            b"CreateService",
        ],
        "process_injection": [
            b"WriteProcessMemory",
            b"CreateRemoteThread",
            b"VirtualAllocEx",
            b"NtUnmapViewOfSection",
        ],
        "ransomware": [
            b".locked",
            b".encrypted",
            b"Your files have been encrypted",
            b"send bitcoin",
            b".onion",
        ],
        "antivm": [
            b"VMWARE",
            b"VBOX",
            b"VirtualBox",
            b"sandbox",
            b"IsDebuggerPresent",
            b"CheckRemoteDebuggerPresent",
        ],
    }

    SUSPICIOUS_STRINGS = [
        (r"\\\\[a-zA-Z0-9]+\\[a-zA-Z]+\\$", "network_share_access", "Network share access pattern"),
        (r"(?:[0-9]{1,3}\\.){3}[0-9]{1,3}:[0-9]+", "ip_port_pattern", "IP:Port pattern in binary"),
        (r"[A-Za-z0-9+/]{40,}={0,2}", "base64_blob", "Long base64 encoded data"),
        (r"\\b(?:SELECT|INSERT|UPDATE|DELETE|DROP)\\s+.*\\b(?:FROM|INTO|TABLE)\\b", "sql_injection", "SQL injection pattern"),
        (r"<script[^>]*>.*?</script>", "embedded_script", "Embedded JavaScript/HTML"),
        (r"eval\\s*\\(", "eval_function", "Eval function usage"),
        (r"system\\s*\\(", "system_function", "System function usage"),
        (r"exec\\s*\\(", "exec_function", "Exec function usage"),
    ]

    PE_SECTION_NAMES = [
        b".text", b".rdata", b".data", b".bss", b".idata",
        b".edata", b".rsrc", b".reloc", b".tls", b".pdata",
    ]

    SUSPICIOUS_SECTION_NAMES = [
        b"UPX0", b"UPX1", b"UPX!",
        b".aspack", b"ASPack",
        b"PECompact", b"PEC2",
        b"Themida", b"WinLicense",
        b"VMP0", b"VMProtect",
        b".MPRESS1", b".MPRESS2",
    ]

    def __init__(self):
        self._compiled_patterns = []
        for pattern, name, desc in self.SUSPICIOUS_STRINGS:
            try:
                self._compiled_patterns.append((re.compile(pattern, re.IGNORECASE), name, desc))
            except re.error:
                pass

    def calculate_entropy(self, data: bytes) -> float:
        if not data:
            return 0.0

        length = len(data)
        frequency = Counter(data)
        entropy = 0.0

        for count in frequency.values():
            probability = count / length
            entropy -= probability * math.log2(probability)

        return entropy

    def calculate_block_entropy(self, data: bytes, block_size: int = 256) -> List[Tuple[int, float]]:
        results = []
        for i in range(0, len(data), block_size):
            block = data[i:i + block_size]
            if len(block) >= block_size // 2:
                entropy = self.calculate_entropy(block)
                results.append((i, entropy))
        return results

    def detect_high_entropy_regions(self, data: bytes) -> List[HeuristicFinding]:
        findings = []
        if not data:
            return findings

        overall_entropy = self.calculate_entropy(data)

        if overall_entropy > self.ENTROPY_THRESHOLD_HIGH:
            findings.append(HeuristicFinding(
                type="entropy",
                name="high_entropy_overall",
                description=f"Overall file entropy is very high ({overall_entropy:.2f}), indicating possible encryption or packing",
                severity="high",
                details=f"Entropy: {overall_entropy:.4f}",
            ))
        elif overall_entropy > self.ENTROPY_THRESHOLD_MEDIUM:
            findings.append(HeuristicFinding(
                type="entropy",
                name="medium_entropy_overall",
                description=f"Overall file entropy is elevated ({overall_entropy:.2f}), suggesting compression or packing",
                severity="medium",
                details=f"Entropy: {overall_entropy:.4f}",
            ))

        block_entropies = self.calculate_block_entropy(data)
        high_entropy_blocks = [(offset, ent) for offset, ent in block_entropies if ent > self.ENTROPY_THRESHOLD_HIGH]

        if len(high_entropy_blocks) > len(block_entropies) * 0.3 and len(block_entropies) > 0:
            findings.append(HeuristicFinding(
                type="entropy",
                name="high_entropy_regions",
                description=f"Multiple high-entropy regions detected ({len(high_entropy_blocks)} blocks > {self.ENTROPY_THRESHOLD_HIGH})",
                severity="high",
                details=f"High entropy blocks at offsets: {[offset for offset, _ in high_entropy_blocks[:5]]}",
            ))

        return findings

    def detect_malware_signatures(self, data: bytes) -> List[HeuristicFinding]:
        findings = []
        data_lower = data.lower()

        for category, signatures in self.MALWARE_SIGNATURES.items():
            for sig in signatures:
                sig_lower = sig.lower()
                offset = data_lower.find(sig_lower)
                if offset >= 0:
                    findings.append(HeuristicFinding(
                        type="signature",
                        name=category,
                        description=f"Found malware signature: {sig.decode('utf-8', errors='ignore')[:80]}",
                        severity=self._get_signature_severity(category),
                        offset=offset,
                        details=f"Signature matched at offset {offset}",
                    ))

        return findings

    def detect_suspicious_strings(self, data: bytes) -> List[HeuristicFinding]:
        findings = []
        try:
            text = data.decode('utf-8', errors='ignore')
        except Exception:
            return findings

        for pattern, name, desc in self._compiled_patterns:
            matches = list(pattern.finditer(text))
            for match in matches[:3]:
                findings.append(HeuristicFinding(
                    type="string_pattern",
                    name=name,
                    description=desc,
                    severity="medium",
                    offset=match.start(),
                    details=f"Matched: {match.group()[:60]}",
                ))

        return findings

    def analyze_pe_structure(self, data: bytes) -> List[HeuristicFinding]:
        findings = []

        if len(data) < 64 or data[:2] != b"MZ":
            return findings

        try:
            pe_offset = struct.unpack_from('<I', data, 0x3C)[0]
            if pe_offset >= len(data) - 24:
                findings.append(HeuristicFinding(
                    type="pe_structure",
                    name="invalid_pe_header",
                    description="Invalid PE header offset",
                    severity="medium",
                ))
                return findings

            pe_header = data[pe_offset:pe_offset + 24]
            if pe_header[:4] != b"PE\x00\x00":
                findings.append(HeuristicFinding(
                    type="pe_structure",
                    name="invalid_pe_signature",
                    description="PE signature not found at expected offset",
                    severity="high",
                    offset=pe_offset,
                ))
                return findings

            machine = struct.unpack_from('<H', pe_header, 4)[0]
            sections_count = struct.unpack_from('<H', pe_header, 6)[0]

            if sections_count > 96:
                findings.append(HeuristicFinding(
                    type="pe_structure",
                    name="suspicious_section_count",
                    description=f"Unusually high number of sections: {sections_count}",
                    severity="medium",
                ))

            is_64bit = machine == 0x8664

            optional_header_size = struct.unpack_from('<H', pe_header, 20)[0]
            optional_header_start = pe_offset + 24
            optional_header_end = optional_header_start + optional_header_size

            if optional_header_end > len(data):
                findings.append(HeuristicFinding(
                    type="pe_structure",
                    name="truncated_optional_header",
                    description="PE optional header is truncated",
                    severity="high",
                ))
                return findings

            magic = struct.unpack_from('<H', data, optional_header_start)[0]
            if magic == 0x20B:
                is_64bit = True
            elif magic == 0x10B:
                is_64bit = False

            if is_64bit:
                entry_point = struct.unpack_from('<I', data, optional_header_start + 16)[0]
                image_base = struct.unpack_from('<Q', data, optional_header_start + 24)[0]
            else:
                entry_point = struct.unpack_from('<I', data, optional_header_start + 16)[0]
                image_base = struct.unpack_from('<I', data, optional_header_start + 28)[0]

            sections_start = optional_header_end
            section_entry_size = 40

            suspicious_sections_found = []
            for i in range(min(sections_count, 96)):
                section_offset = sections_start + i * section_entry_size
                if section_offset + section_entry_size > len(data):
                    break

                section_data = data[section_offset:section_offset + section_entry_size]
                name = section_data[:8].rstrip(b'\x00').rstrip(b' ')

                for sus_name in self.SUSPICIOUS_SECTION_NAMES:
                    if sus_name in name:
                        suspicious_sections_found.append(name.decode('utf-8', errors='ignore'))

                if name not in self.PE_SECTION_NAMES and name not in self.SUSPICIOUS_SECTION_NAMES:
                    if name and name[0:1] != b'.':
                        findings.append(HeuristicFinding(
                            type="pe_structure",
                            name="unusual_section_name",
                            description=f"Unusual section name: {name.decode('utf-8', errors='ignore')}",
                            severity="medium",
                            offset=section_offset,
                        ))

                try:
                    virtual_size = struct.unpack_from('<I', section_data, 8)[0]
                    raw_size = struct.unpack_from('<I', section_data, 16)[0]
                    characteristics = struct.unpack_from('<I', section_data, 36)[0]

                    if virtual_size > 0 and raw_size == 0:
                        pass
                    elif raw_size > virtual_size * 10 and virtual_size > 0:
                        findings.append(HeuristicFinding(
                            type="pe_structure",
                            name="suspicious_section_size",
                            description=f"Section '{name.decode('utf-8', errors='ignore')}' has abnormal size ratio",
                            severity="medium",
                            offset=section_offset,
                        ))

                    if characteristics & 0x20000000:
                        pass
                except Exception:
                    pass

            if suspicious_sections_found:
                findings.append(HeuristicFinding(
                    type="pe_structure",
                    name="packed_section_detected",
                    description=f"Detected packer section names: {', '.join(suspicious_sections_found)}",
                    severity="high",
                ))

            entry_point_section = self._find_section_for_rva(
                data, entry_point, sections_start, sections_count, section_entry_size
            )
            if entry_point_section:
                ep_name = entry_point_section.rstrip(b'\x00').decode('utf-8', errors='ignore')
                if ep_name not in ['.text', '']:
                    findings.append(HeuristicFinding(
                        type="pe_structure",
                        name="entry_point_in_suspicious_section",
                        description=f"Entry point is in non-standard section: {ep_name}",
                        severity="medium",
                        offset=entry_point,
                    ))

        except Exception as e:
            findings.append(HeuristicFinding(
                type="pe_structure",
                name="pe_analysis_error",
                description=f"Error analyzing PE structure: {str(e)}",
                severity="low",
            ))

        return findings

    def _find_section_for_rva(
        self, data: bytes, rva: int, sections_start: int,
        sections_count: int, section_entry_size: int
    ) -> Optional[bytes]:
        for i in range(min(sections_count, 96)):
            section_offset = sections_start + i * section_entry_size
            if section_offset + section_entry_size > len(data):
                break

            section_data = data[section_offset:section_offset + section_entry_size]
            virtual_address = struct.unpack_from('<I', section_data, 12)[0]
            virtual_size = struct.unpack_from('<I', section_data, 8)[0]

            if virtual_address <= rva < virtual_address + virtual_size:
                return section_data[:8]

        return None

    def detect_telltale_signs(self, data: bytes) -> List[HeuristicFinding]:
        findings = []

        if b"PK\x03\x04" in data[:4]:
            pass
        elif b"MZ" in data[:2]:
            pass

        shellcode_patterns = [
            (b"\\x55\\x8B\\xEC", "push_ebp_mov_ebp_esp", "Function prologue in shellcode"),
            (b"\\x31\\xC0\\x50\\x68", "xor_eax_eax_push_pattern", "Shellcode stack pattern"),
        ]

        return findings

    def scan(self, data: bytes, filename: str = "") -> Dict:
        findings: List[HeuristicFinding] = []

        try:
            findings.extend(self.detect_high_entropy_regions(data))
        except Exception as e:
            findings.append(HeuristicFinding(
                type="error",
                name="entropy_analysis_error",
                description=f"Entropy analysis error: {str(e)}",
                severity="low",
            ))

        try:
            findings.extend(self.detect_malware_signatures(data))
        except Exception as e:
            findings.append(HeuristicFinding(
                type="error",
                name="signature_detection_error",
                description=f"Signature detection error: {str(e)}",
                severity="low",
            ))

        try:
            findings.extend(self.detect_suspicious_strings(data))
        except Exception as e:
            pass

        try:
            findings.extend(self.analyze_pe_structure(data))
        except Exception as e:
            pass

        entropy = self.calculate_entropy(data) if data else 0.0

        severity = "clean"
        if findings:
            max_severity = "low"
            for f in findings:
                if f.severity in ("critical", "high", "medium", "low"):
                    order = {"critical": 4, "high": 3, "medium": 2, "low": 1}
                    if order.get(f.severity, 0) > order.get(max_severity, 0):
                        max_severity = f.severity
            severity = max_severity

        return {
            "enabled": True,
            "findings": [
                {
                    "type": f.type,
                    "name": f.name,
                    "description": f.description,
                    "severity": f.severity,
                    "offset": f.offset,
                    "details": f.details,
                }
                for f in findings
            ],
            "total_findings": len(findings),
            "highest_severity": severity,
            "entropy": round(entropy, 4),
            "entropy_category": (
                "high" if entropy > 7.5 else "medium" if entropy > 7.0 else "normal"
            ),
        }

    def _get_signature_severity(self, category: str) -> str:
        severity_map = {
            "credential_theft": "critical",
            "ransomware": "critical",
            "reverse_shell": "critical",
            "process_injection": "high",
            "download_execute": "high",
            "backdoor_cmdshell": "high",
            "persistence": "medium",
            "antivm": "medium",
        }
        return severity_map.get(category, "medium")
