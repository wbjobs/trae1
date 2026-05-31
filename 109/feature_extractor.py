import math
import time
from collections import defaultdict, Counter
from typing import Dict, List, Tuple

class FlowFeatures:
    def __init__(self):
        self.packet_sizes = []
        self.protocols = []
        self.flows = defaultdict(lambda: {'start_time': None, 'end_time': None, 'packets': 0, 'bytes': 0, 'flags': []})
        self.src_ips = []
        self.dst_ips = []
        self.src_ports = []
        self.dst_ports = []
        self.timestamps = []
        self.tcp_flags = []
    
    def add_packet(self, src_ip: str, dst_ip: str, protocol: int, src_port: int, dst_port: int, 
                   packet_size: int, flags: int, timestamp: float):
        self.src_ips.append(src_ip)
        self.dst_ips.append(dst_ip)
        self.protocols.append(protocol)
        self.src_ports.append(src_port)
        self.dst_ports.append(dst_port)
        self.packet_sizes.append(packet_size)
        self.timestamps.append(timestamp)
        self.tcp_flags.append(flags)
        
        flow_key = f"{src_ip}:{src_port}-{dst_ip}:{dst_port}"
        flow = self.flows[flow_key]
        if flow['start_time'] is None:
            flow['start_time'] = timestamp
        flow['end_time'] = timestamp
        flow['packets'] += 1
        flow['bytes'] += packet_size
        flow['flags'].append(flags)
    
    def get_features(self) -> Dict[str, float]:
        features = {}
        
        features['num_packets'] = len(self.packet_sizes)
        features['num_flows'] = len(self.flows)
        features['num_src_ips'] = len(set(self.src_ips))
        features['num_dst_ips'] = len(set(self.dst_ips))
        
        features['packet_size_mean'] = self._mean(self.packet_sizes) if self.packet_sizes else 0
        features['packet_size_std'] = self._std(self.packet_sizes) if len(self.packet_sizes) > 1 else 0
        features['packet_size_min'] = min(self.packet_sizes) if self.packet_sizes else 0
        features['packet_size_max'] = max(self.packet_sizes) if self.packet_sizes else 0
        
        features['protocol_tcp_ratio'] = self.protocols.count(6) / max(len(self.protocols), 1)
        features['protocol_udp_ratio'] = self.protocols.count(17) / max(len(self.protocols), 1)
        features['protocol_icmp_ratio'] = self.protocols.count(1) / max(len(self.protocols), 1)
        
        features['src_ip_entropy'] = self._entropy(self.src_ips)
        features['dst_ip_entropy'] = self._entropy(self.dst_ips)
        features['src_port_entropy'] = self._entropy([str(p) for p in self.src_ports])
        features['dst_port_entropy'] = self._entropy([str(p) for p in self.dst_ports])
        
        flow_durations = []
        for flow in self.flows.values():
            if flow['start_time'] and flow['end_time']:
                flow_durations.append(flow['end_time'] - flow['start_time'])
        features['flow_duration_mean'] = self._mean(flow_durations) if flow_durations else 0
        features['flow_duration_std'] = self._std(flow_durations) if len(flow_durations) > 1 else 0
        
        packets_per_flow = [f['packets'] for f in self.flows.values()]
        features['packets_per_flow_mean'] = self._mean(packets_per_flow) if packets_per_flow else 0
        features['packets_per_flow_std'] = self._std(packets_per_flow) if len(packets_per_flow) > 1 else 0
        
        bytes_per_flow = [f['bytes'] for f in self.flows.values()]
        features['bytes_per_flow_mean'] = self._mean(bytes_per_flow) if bytes_per_flow else 0
        features['bytes_per_flow_std'] = self._std(bytes_per_flow) if len(bytes_per_flow) > 1 else 0
        
        features['syn_ratio'] = self._flag_ratio(0x02)
        features['ack_ratio'] = self._flag_ratio(0x10)
        features['fin_ratio'] = self._flag_ratio(0x01)
        features['rst_ratio'] = self._flag_ratio(0x04)
        features['syn_ack_ratio'] = self._syn_ack_ratio()
        
        features['dns_query_ratio'] = self._port_ratio(53)
        features['http_ratio'] = self._port_ratio(80) + self._port_ratio(8080)
        features['https_ratio'] = self._port_ratio(443)
        features['ntp_ratio'] = self._port_ratio(123)
        
        common_ports = [21, 22, 23, 25, 53, 80, 443, 3306, 8080]
        for port in common_ports:
            features[f'port_{port}_ratio'] = self._port_ratio(port)
        
        if self.timestamps:
            time_span = max(self.timestamps) - min(self.timestamps)
            features['time_span'] = time_span
            features['packet_rate'] = len(self.packet_sizes) / max(time_span, 1)
            features['bytes_per_second'] = sum(self.packet_sizes) / max(time_span, 1)
        
        unique_src_ips = len(set(self.src_ips))
        features['unique_src_ips_per_flow'] = unique_src_ips / max(len(self.flows), 1)
        
        return features
    
    def _mean(self, values: List[float]) -> float:
        return sum(values) / len(values) if values else 0
    
    def _std(self, values: List[float]) -> float:
        if len(values) < 2:
            return 0
        mean = self._mean(values)
        variance = sum((x - mean) ** 2 for x in values) / (len(values) - 1)
        return math.sqrt(variance)
    
    def _entropy(self, items: List[str]) -> float:
        if not items:
            return 0
        counter = Counter(items)
        total = len(items)
        entropy = 0
        for count in counter.values():
            if count > 0:
                p = count / total
                entropy -= p * math.log2(p)
        return entropy
    
    def _flag_ratio(self, flag: int) -> float:
        if not self.tcp_flags:
            return 0
        return sum(1 for f in self.tcp_flags if f & flag) / len(self.tcp_flags)
    
    def _syn_ack_ratio(self) -> float:
        if not self.tcp_flags:
            return 0
        syn_count = sum(1 for f in self.tcp_flags if f & 0x02)
        ack_count = sum(1 for f in self.tcp_flags if f & 0x10)
        return syn_count / max(ack_count, 1)
    
    def _port_ratio(self, port: int) -> float:
        all_ports = self.src_ports + self.dst_ports
        if not all_ports:
            return 0
        return all_ports.count(port) / len(all_ports)
    
    def get_feature_vector(self) -> Tuple[List[float], List[str]]:
        features = self.get_features()
        feature_names = sorted(features.keys())
        feature_vector = [features[name] for name in feature_names]
        return feature_vector, feature_names

class FeatureExtractor:
    def __init__(self, window_size: int = 60):
        self.window_size = window_size
        self.flow_features = FlowFeatures()
        self.window_start = time.time()
    
    def add_sample(self, src_ip: str, dst_ip: str, protocol: int, src_port: int, dst_port: int,
                   packet_size: int, flags: int):
        current_time = time.time()
        
        if current_time - self.window_start > self.window_size:
            self.flow_features = FlowFeatures()
            self.window_start = current_time
        
        self.flow_features.add_packet(src_ip, dst_ip, protocol, src_port, dst_port, 
                                     packet_size, flags, current_time)
    
    def get_current_features(self) -> Dict[str, float]:
        return self.flow_features.get_features()
    
    def get_feature_vector(self) -> Tuple[List[float], List[str]]:
        return self.flow_features.get_feature_vector()
    
    def reset(self):
        self.flow_features = FlowFeatures()
        self.window_start = time.time()
