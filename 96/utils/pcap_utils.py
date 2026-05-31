import os
from typing import List, Optional
from protocols.modbus_tcp import ModbusTCP
from protocols.dnp3 import DNP3


def load_pcap_seeds(pcap_file: str, protocol: str) -> List[bytes]:
    try:
        from scapy.all import rdpcap
    except ImportError:
        print("[-] Scapy not installed. Install with: pip install scapy")
        return []
    
    if not os.path.exists(pcap_file):
        print(f"[-] PCAP file not found: {pcap_file}")
        return []
    
    try:
        packets = rdpcap(pcap_file)
        print(f"[+] Loaded {len(packets)} packets from {pcap_file}")
    except Exception as e:
        print(f"[-] Error reading PCAP file: {e}")
        return []
    
    seed_packets = []
    
    if protocol.lower() == 'modbus':
        seed_packets = ModbusTCP.extract_packets_from_pcap(packets)
    elif protocol.lower() == 'dnp3':
        seed_packets = DNP3.extract_packets_from_pcap(packets)
    
    print(f"[+] Extracted {len(seed_packets)} {protocol.upper()} request packets as seeds")
    
    if seed_packets:
        sample = seed_packets[0]
        print(f"[+] Sample seed packet (len={len(sample)}): {sample[:20].hex()}...")
    
    return seed_packets


def save_crash_packet(data: bytes, protocol: str, crash_reason: str, crash_dir: str = './crashes') -> Optional[str]:
    import time
    import json
    
    os.makedirs(crash_dir, exist_ok=True)
    
    timestamp = time.strftime('%Y%m%d_%H%M%S')
    filename = f"{protocol}_{timestamp}_{crash_reason.replace(' ', '_')}.bin"
    filepath = os.path.join(crash_dir, filename)
    
    try:
        with open(filepath, 'wb') as f:
            f.write(data)
        
        metadata = {
            'protocol': protocol,
            'timestamp': timestamp,
            'crash_reason': crash_reason,
            'packet_length': len(data),
            'packet_hex': data.hex(),
        }
        meta_filepath = filepath + '.json'
        with open(meta_filepath, 'w') as f:
            json.dump(metadata, f, indent=2)
        
        print(f"[!] Crash saved to: {filepath}")
        return filepath
    except Exception as e:
        print(f"[-] Error saving crash packet: {e}")
        return None


def load_crash_packet(filepath: str) -> Optional[bytes]:
    if not os.path.exists(filepath):
        print(f"[-] Crash file not found: {filepath}")
        return None
    
    try:
        with open(filepath, 'rb') as f:
            data = f.read()
        print(f"[+] Loaded crash packet ({len(data)} bytes) from {filepath}")
        return data
    except Exception as e:
        print(f"[-] Error loading crash packet: {e}")
        return None


def list_crash_files(crash_dir: str = './crashes') -> List[str]:
    if not os.path.exists(crash_dir):
        return []
    
    crash_files = []
    for f in os.listdir(crash_dir):
        if f.endswith('.bin'):
            filepath = os.path.join(crash_dir, f)
            crash_files.append(filepath)
    
    return sorted(crash_files)
