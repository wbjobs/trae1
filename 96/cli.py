#!/usr/bin/env python3
import argparse
import sys
import os
import socket

from fuzzer import ProtocolFuzzer
from utils.pcap_utils import load_pcap_seeds, load_crash_packet, list_crash_files
from protocols.modbus_tcp import ModbusTCP
from protocols.dnp3 import DNP3


def validate_host(host: str) -> bool:
    try:
        socket.gethostbyname(host)
        return True
    except socket.gaierror:
        return False


def validate_port(port: int) -> bool:
    return 1 <= port <= 65535


def get_default_port(protocol: str) -> int:
    if protocol.lower() == 'modbus':
        return 502
    elif protocol.lower() == 'dnp3':
        return 20000
    return 0


def cmd_fuzz(args):
    target_host = args.target
    target_port = args.port or get_default_port(args.protocol)
    
    if not validate_host(target_host):
        print(f"[-] Invalid target host: {target_host}")
        sys.exit(1)
    
    if not validate_port(target_port):
        print(f"[-] Invalid target port: {target_port}")
        sys.exit(1)
    
    seed_packets = []
    
    if args.pcap:
        seed_packets = load_pcap_seeds(args.pcap, args.protocol)
    else:
        print(f"[*] No PCAP file provided, using generated sample packets")
        if args.protocol.lower() == 'modbus':
            seed_packets = ModbusTCP.generate_sample_packets()
        elif args.protocol.lower() == 'dnp3':
            seed_packets = DNP3.generate_sample_packets()
    
    if not seed_packets:
        print("[-] No seed packets available. Exiting.")
        sys.exit(1)
    
    print(f"[*] Using {len(seed_packets)} seed packets")
    
    fuzzer = ProtocolFuzzer(
        target_host=target_host,
        target_port=target_port,
        protocol=args.protocol,
        seed_packets=seed_packets,
        timeout=args.timeout,
        num_threads=args.threads,
        use_genetic=not args.no_genetic,
        crash_dir=args.crash_dir,
        smart_fuzz=not args.no_smart_fuzz
    )
    
    fuzzer.start(max_iterations=args.iterations)


def cmd_replay(args):
    target_host = args.target
    target_port = args.port or get_default_port(args.protocol)
    
    if not validate_host(target_host):
        print(f"[-] Invalid target host: {target_host}")
        sys.exit(1)
    
    if not validate_port(target_port):
        print(f"[-] Invalid target port: {target_port}")
        sys.exit(1)
    
    crash_file = args.replay
    
    if crash_file == 'list':
        crash_files = list_crash_files(args.crash_dir)
        if crash_files:
            print(f"\nAvailable crash files in {args.crash_dir}/:")
            for i, f in enumerate(crash_files, 1):
                print(f"  {i}. {os.path.basename(f)}")
            print()
        else:
            print(f"[*] No crash files found in {args.crash_dir}/")
        return
    
    if crash_file.isdigit():
        crash_files = list_crash_files(args.crash_dir)
        idx = int(crash_file) - 1
        if 0 <= idx < len(crash_files):
            crash_file = crash_files[idx]
        else:
            print(f"[-] Invalid crash file index: {crash_file}")
            sys.exit(1)
    
    packet_data = load_crash_packet(crash_file)
    if not packet_data:
        sys.exit(1)
    
    fuzzer = ProtocolFuzzer(
        target_host=target_host,
        target_port=target_port,
        protocol=args.protocol,
        seed_packets=[packet_data],
        timeout=args.timeout,
        num_threads=1,
        use_genetic=False,
        crash_dir=args.crash_dir
    )
    
    results = fuzzer.replay_packet(packet_data, num_times=args.count)
    
    crashes = sum(1 for r in results if r.is_crash)
    exceptions = sum(1 for r in results if r.has_exception)
    timeouts = sum(1 for r in results if r.is_timeout)
    
    print(f"\nReplay complete: {crashes} crashes, {exceptions} exceptions, {timeouts} timeouts out of {len(results)} attempts")
    
    if crashes > 0:
        print("[!] Crash was successfully reproduced!")
    else:
        print("[*] Crash was not reproduced in this session")


def cmd_list(args):
    crash_files = list_crash_files(args.crash_dir)
    
    if not crash_files:
        print(f"[*] No crash files found in {args.crash_dir}/")
        return
    
    print(f"\nCrash files in {args.crash_dir}/:")
    print("-" * 80)
    
    for i, filepath in enumerate(crash_files, 1):
        filename = os.path.basename(filepath)
        try:
            with open(filepath, 'rb') as f:
                size = len(f.read())
            
            meta_file = filepath + '.json'
            reason = "Unknown"
            protocol = args.protocol
            
            if os.path.exists(meta_file):
                import json
                with open(meta_file, 'r') as f:
                    meta = json.load(f)
                    reason = meta.get('crash_reason', reason)
                    protocol = meta.get('protocol', protocol)
            
            print(f"  [{i:2d}] {filename:<50s} {size:>6d} bytes  {reason}")
            
        except Exception as e:
            print(f"  [{i:2d}] {filename:<50s} Error: {e}")
    
    print("-" * 80)
    print(f"Total: {len(crash_files)} crash files\n")


def cmd_protocols(args):
    print("\nSupported Protocols:")
    print("=" * 60)
    
    print("\n1. Modbus TCP")
    print("-" * 60)
    mb_info = ModbusTCP.get_protocol_info()
    print(f"   Default Port: {mb_info['port']}")
    print("   Function Codes:")
    for code, name in mb_info['function_codes'].items():
        print(f"     0x{code:02X} - {name}")
    
    print("\n2. DNP3")
    print("-" * 60)
    dnp3_info = DNP3.get_protocol_info()
    print(f"   Default Port: {dnp3_info['port']}")
    print("   Selected Function Codes:")
    for code, name in list(dnp3_info['function_codes'].items())[:15]:
        print(f"     0x{code:02X} - {name}")
    print("     ... (and more)")
    
    print("\n" + "=" * 60)


def main():
    parser = argparse.ArgumentParser(
        description='ICS Protocol Fuzzer - Modbus TCP and DNP3',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Fuzz Modbus TCP with PCAP seeds
  %(prog)s fuzz --protocol modbus --target 192.168.1.100 --pcap capture.pcap
  
  # Fuzz DNP3 with generated packets, 8 threads
  %(prog)s fuzz --protocol dnp3 --target 192.168.1.101 --threads 8 --iterations 100
  
  # Replay a crash file
  %(prog)s replay --protocol modbus --target 192.168.1.100 --replay crashes/modbus_xxx.bin
  
  # List crash files
  %(prog)s list
  
  # Show supported protocols
  %(prog)s protocols
        """
    )
    
    subparsers = parser.add_subparsers(dest='command', help='Available commands')
    
    fuzz_parser = subparsers.add_parser('fuzz', help='Start fuzzing')
    fuzz_parser.add_argument('--protocol', required=True, choices=['modbus', 'dnp3'],
                            help='Protocol to fuzz')
    fuzz_parser.add_argument('--target', required=True, help='Target IP or hostname')
    fuzz_parser.add_argument('--port', type=int, help='Target port (default: 502 for Modbus, 20000 for DNP3)')
    fuzz_parser.add_argument('--pcap', help='PCAP file for seed packets')
    fuzz_parser.add_argument('--threads', type=int, default=4, help='Number of threads (default: 4)')
    fuzz_parser.add_argument('--iterations', type=int, default=1000, help='Number of iterations (default: 1000)')
    fuzz_parser.add_argument('--timeout', type=float, default=3.0, help='Response timeout in seconds (default: 3.0)')
    fuzz_parser.add_argument('--no-genetic', action='store_true', help='Disable genetic algorithm')
    fuzz_parser.add_argument('--no-smart-fuzz', action='store_true', help='Disable protocol-aware smart fuzzing')
    fuzz_parser.add_argument('--crash-dir', default='./crashes', help='Directory to save crash files')
    
    replay_parser = subparsers.add_parser('replay', help='Replay a crash packet')
    replay_parser.add_argument('--protocol', required=True, choices=['modbus', 'dnp3'],
                              help='Protocol of the crash packet')
    replay_parser.add_argument('--target', required=True, help='Target IP or hostname')
    replay_parser.add_argument('--port', type=int, help='Target port')
    replay_parser.add_argument('--replay', required=True,
                              help='Crash file to replay, or "list" to show available, or index number')
    replay_parser.add_argument('--count', type=int, default=3, help='Number of replay attempts (default: 3)')
    replay_parser.add_argument('--timeout', type=float, default=3.0, help='Response timeout')
    replay_parser.add_argument('--crash-dir', default='./crashes', help='Directory with crash files')
    
    list_parser = subparsers.add_parser('list', help='List saved crash files')
    list_parser.add_argument('--crash-dir', default='./crashes', help='Directory with crash files')
    
    subparsers.add_parser('protocols', help='Show supported protocols')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        sys.exit(1)
    
    if args.command == 'fuzz':
        cmd_fuzz(args)
    elif args.command == 'replay':
        cmd_replay(args)
    elif args.command == 'list':
        cmd_list(args)
    elif args.command == 'protocols':
        cmd_protocols(args)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print("\n[!] Interrupted by user")
        sys.exit(0)
    except Exception as e:
        print(f"\n[!] Fatal error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
