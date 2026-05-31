import socket
import time
import threading
import queue
from typing import Dict, List, Optional, Tuple, Any
from dataclasses import dataclass, field

from utils.mutations import Mutator
from utils.genetic import GeneticStrategy, Chromosome
from utils.pcap_utils import save_crash_packet
from protocols.modbus_tcp import ModbusTCP
from protocols.dnp3 import DNP3


@dataclass
class FuzzResult:
    packet: bytes
    mutation_type: str
    response_time: float
    response_data: Optional[bytes]
    is_timeout: bool
    has_exception: bool
    is_crash: bool
    crash_reason: str
    chromosome: Optional[Chromosome] = None


@dataclass
class FuzzStats:
    total_packets: int = 0
    crashes: int = 0
    exceptions: int = 0
    timeouts: int = 0
    crash_files: List[str] = field(default_factory=list)
    start_time: float = 0.0

    def elapsed(self) -> float:
        return time.time() - self.start_time

    def packets_per_second(self) -> float:
        elapsed = self.elapsed()
        if elapsed == 0:
            return 0.0
        return self.total_packets / elapsed


class ProtocolFuzzer:
    def __init__(
        self,
        target_host: str,
        target_port: int,
        protocol: str,
        seed_packets: List[bytes],
        timeout: float = 3.0,
        num_threads: int = 4,
        use_genetic: bool = True,
        crash_dir: str = './crashes',
        smart_fuzz: bool = True
    ):
        self.target_host = target_host
        self.target_port = target_port
        self.protocol = protocol.lower()
        self.seed_packets = seed_packets
        self.timeout = timeout
        self.num_threads = num_threads
        self.use_genetic = use_genetic
        self.crash_dir = crash_dir
        self.smart_fuzz = smart_fuzz
        
        self.mutator = Mutator()
        self.genetic = GeneticStrategy(self.mutator) if use_genetic else None
        
        self.protocol_handler = self._get_protocol_handler()
        
        self.stats = FuzzStats()
        self.stats_lock = threading.Lock()
        
        self.work_queue = queue.Queue()
        self.result_queue = queue.Queue()
        
        self.stop_event = threading.Event()
        self.threads: List[threading.Thread] = []
        
        self.current_chromosomes: Dict[int, Chromosome] = {}
        self.chromosome_lock = threading.Lock()
        
        self.initial_probe_passed = False
        
        self._validate_seed_packets()

    def _get_protocol_handler(self):
        if self.protocol == 'modbus':
            return ModbusTCP
        elif self.protocol == 'dnp3':
            return DNP3
        else:
            raise ValueError(f"Unsupported protocol: {self.protocol}")

    def _validate_seed_packets(self):
        if not self.seed_packets:
            return
        
        valid_packets = []
        for packet in self.seed_packets:
            if self.smart_fuzz and hasattr(self.protocol_handler, 'validate_fields'):
                if self.protocol == 'modbus':
                    parsed = ModbusTCP.parse_request(packet)
                elif self.protocol == 'dnp3':
                    parsed = DNP3.parse_full_packet(packet)
                else:
                    parsed = None
                
                if parsed:
                    is_valid, reason = self.protocol_handler.validate_fields(parsed)
                    if is_valid:
                        valid_packets.append(packet)
                    else:
                        print(f"[*] Filtering invalid seed packet: {reason}")
                else:
                    valid_packets.append(packet)
            else:
                valid_packets.append(packet)
        
        if len(valid_packets) < len(self.seed_packets):
            print(f"[*] Filtered {len(self.seed_packets) - len(valid_packets)} invalid seed packets")
            self.seed_packets = valid_packets

    def probe_target(self) -> bool:
        print(f"[*] Probing target {self.target_host}:{self.target_port}...")
        
        sample_packets = self.protocol_handler.generate_sample_packets()
        if not sample_packets:
            print("[-] No sample packets available for probing")
            return False
        
        test_packet = sample_packets[0]
        print(f"[*] Sending probe packet ({len(test_packet)} bytes)...")
        
        sock = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self.timeout)
            sock.connect((self.target_host, self.target_port))
            sock.sendall(test_packet)
            
            response = self._receive_response(sock)
            if response:
                print(f"[+] Target responded with {len(response)} bytes")
                is_exception, reason = self.protocol_handler.is_exception_response(response)
                if is_exception:
                    print(f"[*] Response indicates exception: {reason}")
                self.initial_probe_passed = True
                return True
            else:
                print("[-] No response received from target (timeout)")
                return False
        except ConnectionRefusedError:
            print(f"[-] Connection refused to {self.target_host}:{self.target_port}")
            return False
        except socket.timeout:
            print("[-] Probe timed out")
            return False
        except Exception as e:
            print(f"[-] Error probing target: {e}")
            return False
        finally:
            if sock:
                try:
                    sock.close()
                except:
                    pass

    def _receive_response(self, sock: socket.socket, buffer_size: int = 4096) -> Optional[bytes]:
        try:
            data = b''
            start_time = time.time()
            while time.time() - start_time < self.timeout:
                try:
                    chunk = sock.recv(buffer_size)
                    if not chunk:
                        break
                    data += chunk
                    if len(data) > 0:
                        return data
                except socket.timeout:
                    if len(data) > 0:
                        return data
                    break
            return data if len(data) > 0 else None
        except Exception:
            return None

    def _send_and_receive(self, packet: bytes) -> Tuple[Optional[bytes], float, bool]:
        sock = None
        start_time = time.time()
        is_timeout = False
        
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self.timeout)
            sock.connect((self.target_host, self.target_port))
            sock.sendall(packet)
            
            response = self._receive_response(sock)
            elapsed = time.time() - start_time
            
            if response is None:
                is_timeout = True
            
            return response, elapsed, is_timeout
        except ConnectionResetError:
            elapsed = time.time() - start_time
            return None, elapsed, True
        except ConnectionRefusedError:
            elapsed = time.time() - start_time
            return None, elapsed, True
        except socket.timeout:
            elapsed = time.time() - start_time
            return None, elapsed, True
        except Exception as e:
            elapsed = time.time() - start_time
            return None, elapsed, True
        finally:
            if sock:
                try:
                    sock.close()
                except:
                    pass

    def _check_for_crash(self, packet: bytes, response: Optional[bytes], is_timeout: bool, elapsed: float) -> Tuple[bool, bool, str]:
        is_crash = False
        has_exception = False
        crash_reason = ""
        
        if is_timeout:
            if elapsed > self.timeout * 2:
                is_crash = True
                crash_reason = "Connection_timeout_or_reset"
            else:
                has_exception = True
                crash_reason = "Timeout"
        elif response is None:
            is_crash = True
            crash_reason = "Connection_reset"
        else:
            is_exception, reason = self.protocol_handler.is_exception_response(response)
            if is_exception:
                has_exception = True
                crash_reason = reason
                
                if 'Device Restart' in crash_reason or 'Device Failure' in crash_reason:
                    is_crash = True
        
        return is_crash, has_exception, crash_reason

    def _worker_thread(self, thread_id: int):
        while not self.stop_event.is_set():
            try:
                work_item = self.work_queue.get(timeout=1.0)
                if work_item is None:
                    break
                
                packet_index, packet_data, mutation_type, chromosome = work_item
                
                response, elapsed, is_timeout = self._send_and_receive(packet_data)
                
                is_crash, has_exception, crash_reason = self._check_for_crash(
                    packet_data, response, is_timeout, elapsed
                )
                
                result = FuzzResult(
                    packet=packet_data,
                    mutation_type=mutation_type,
                    response_time=elapsed,
                    response_data=response,
                    is_timeout=is_timeout,
                    has_exception=has_exception,
                    is_crash=is_crash,
                    crash_reason=crash_reason,
                    chromosome=chromosome
                )
                
                self.result_queue.put((packet_index, result))
                self.work_queue.task_done()
                
            except queue.Empty:
                continue
            except Exception as e:
                print(f"[!] Thread {thread_id} error: {e}")
                continue

    def _monitor_thread(self):
        while not self.stop_event.is_set():
            time.sleep(5)
            with self.stats_lock:
                pps = self.stats.packets_per_second()
                print(f"[*] Status: {self.stats.total_packets} packets, "
                      f"{self.stats.crashes} crashes, {self.stats.exceptions} exceptions, "
                      f"{self.stats.timeouts} timeouts, {pps:.2f} pps")
            
            if self.use_genetic and self.genetic:
                stats = self.genetic.get_stats()
                print(f"[*] Genetic: Gen {stats['generation']}, "
                      f"Best fitness: {stats['best_fitness']:.2f}, "
                      f"Avg fitness: {stats['avg_fitness']:.2f}")

    def _generate_test_cases(self) -> List[Tuple[bytes, str, Optional[Chromosome]]]:
        test_cases = []
        
        if self.use_genetic and self.genetic and self.genetic.population:
            for i, chromosome in enumerate(self.genetic.population):
                test_cases.append((chromosome.data, chromosome.mutation_type or 'genetic', chromosome))
        else:
            for i, seed in enumerate(self.seed_packets):
                mutated_data, mutation_type = self._mutate_with_tracking(seed)
                test_cases.append((mutated_data, mutation_type, None))
        
        return test_cases

    def _mutate_with_tracking(self, data: bytes) -> Tuple[bytes, str]:
        import random
        mutations = list(self.mutator.mutation_weights.keys())
        weights = list(self.mutator.mutation_weights.values())
        mutation_type = random.choices(mutations, weights=weights, k=1)[0]
        
        if self.smart_fuzz and hasattr(self.protocol_handler, 'smart_mutate'):
            return self.protocol_handler.smart_mutate(data, mutation_type, self.mutator)
        
        mutation_methods = {
            'bit_flip': self.mutator.bit_flip,
            'boundary': self.mutator.boundary_value,
            'random_byte': self.mutator.random_byte,
            'length_overflow': self.mutator.length_overflow
        }
        
        return mutation_methods[mutation_type](data), mutation_type

    def _process_results(self, results: List[FuzzResult], generation: int):
        for result in results:
            with self.stats_lock:
                self.stats.total_packets += 1
                
                if result.is_timeout:
                    self.stats.timeouts += 1
                if result.has_exception:
                    self.stats.exceptions += 1
                if result.is_crash:
                    self.stats.crashes += 1
                    
                    crash_file = save_crash_packet(
                        result.packet,
                        self.protocol,
                        result.crash_reason,
                        self.crash_dir
                    )
                    if crash_file:
                        self.stats.crash_files.append(crash_file)
            
            if self.use_genetic and self.genetic and result.chromosome:
                self.genetic.evaluate_fitness(
                    result.chromosome,
                    result.is_crash,
                    result.response_time,
                    result.has_exception
                )

    def start(self, max_iterations: int = 1000):
        print(f"[*] Starting fuzzing against {self.target_host}:{self.target_port}")
        print(f"[*] Protocol: {self.protocol.upper()}")
        print(f"[*] Threads: {self.num_threads}")
        print(f"[*] Genetic algorithm: {'Enabled' if self.use_genetic else 'Disabled'}")
        print(f"[*] Smart fuzz (protocol-aware): {'Enabled' if self.smart_fuzz else 'Disabled'}")
        print(f"[*] Seed packets: {len(self.seed_packets)}")
        print(f"[*] Max iterations: {max_iterations}")
        print()
        
        self.stats.start_time = time.time()
        
        if not self.initial_probe_passed:
            if not self.probe_target():
                print("[-] Failed to connect to target. Exiting.")
                return
        
        if self.use_genetic and self.genetic and self.seed_packets:
            print(f"[*] Initializing genetic algorithm population...")
            self.genetic.initialize_population(self.seed_packets)
        
        print("[*] Starting worker threads...")
        for i in range(self.num_threads):
            t = threading.Thread(target=self._worker_thread, args=(i,), daemon=True)
            t.start()
            self.threads.append(t)
        
        monitor_t = threading.Thread(target=self._monitor_thread, daemon=True)
        monitor_t.start()
        self.threads.append(monitor_t)
        
        try:
            for iteration in range(max_iterations):
                if self.stop_event.is_set():
                    break
                
                print(f"\n[*] Iteration {iteration + 1}/{max_iterations}")
                
                test_cases = self._generate_test_cases()
                
                results: List[Tuple[int, FuzzResult]] = []
                
                for idx, (packet_data, mutation_type, chromosome) in enumerate(test_cases):
                    self.work_queue.put((idx, packet_data, mutation_type, chromosome))
                
                for _ in range(len(test_cases)):
                    try:
                        result = self.result_queue.get(timeout=self.timeout + 5.0)
                        results.append(result)
                    except queue.Empty:
                        print("[!] Timed out waiting for results")
                        break
                
                results.sort(key=lambda x: x[0])
                sorted_results = [r[1] for r in results]
                
                self._process_results(sorted_results, iteration)
                
                for result in sorted_results:
                    if result.is_crash:
                        print(f"[!] CRASH: {result.crash_reason} - {result.mutation_type} - {len(result.packet)} bytes")
                    elif result.has_exception:
                        print(f"[!] Exception: {result.crash_reason} - {result.mutation_type}")
                
                if self.use_genetic and self.genetic:
                    self.genetic.create_next_generation()
            
        except KeyboardInterrupt:
            print("\n[!] Fuzzing stopped by user")
        finally:
            self.stop()
            self._print_final_stats()

    def stop(self):
        print("\n[*] Stopping fuzzer...")
        self.stop_event.set()
        
        for _ in range(self.num_threads):
            self.work_queue.put(None)
        
        for t in self.threads:
            if t.is_alive():
                t.join(timeout=2.0)
        
        print("[*] Fuzzer stopped")

    def _print_final_stats(self):
        print("\n" + "=" * 50)
        print("FUZZING RESULTS")
        print("=" * 50)
        print(f"Total packets sent:    {self.stats.total_packets}")
        print(f"Total crashes:         {self.stats.crashes}")
        print(f"Total exceptions:      {self.stats.exceptions}")
        print(f"Total timeouts:        {self.stats.timeouts}")
        print(f"Elapsed time:          {self.stats.elapsed():.2f}s")
        print(f"Average rate:          {self.stats.packets_per_second():.2f} pps")
        print(f"Smart fuzz mode:       {'Enabled' if self.smart_fuzz else 'Disabled'}")
        
        if self.stats.crash_files:
            print(f"\nCrash files saved to {self.crash_dir}/:")
            for f in self.stats.crash_files:
                print(f"  - {f}")
        else:
            print("\nNo crashes detected.")
        print("=" * 50)

    def replay_packet(self, packet_data: bytes, num_times: int = 1) -> List[FuzzResult]:
        results = []
        
        print(f"[*] Replaying packet ({len(packet_data)} bytes) {num_times} times...")
        
        for i in range(num_times):
            print(f"[*] Replay attempt {i + 1}/{num_times}")
            
            response, elapsed, is_timeout = self._send_and_receive(packet_data)
            is_crash, has_exception, crash_reason = self._check_for_crash(
                packet_data, response, is_timeout, elapsed
            )
            
            result = FuzzResult(
                packet=packet_data,
                mutation_type='replay',
                response_time=elapsed,
                response_data=response,
                is_timeout=is_timeout,
                has_exception=has_exception,
                is_crash=is_crash,
                crash_reason=crash_reason
            )
            
            results.append(result)
            
            if response:
                print(f"[+] Response: {len(response)} bytes, time: {elapsed:.3f}s")
                is_exception, reason = self.protocol_handler.is_exception_response(response)
                if is_exception:
                    print(f"[!] Exception in response: {reason}")
            else:
                print(f"[-] No response, time: {elapsed:.3f}s")
            
            if is_crash:
                print(f"[!] Crash confirmed: {crash_reason}")
            
            if i < num_times - 1:
                time.sleep(0.5)
        
        return results

    def get_stats(self) -> FuzzStats:
        with self.stats_lock:
            return self.stats
