import asyncio
import logging
import socket
import struct
import time
from collections import deque
from typing import Dict, Optional

logger = logging.getLogger(__name__)

class TrafficRecord:
    def __init__(self, src_ip: str, dst_ip: str, protocol: int, src_port: int, dst_port: int, 
                 bytes_count: int, packets: int, flags: int):
        self.timestamp = time.time()
        self.src_ip = src_ip
        self.dst_ip = dst_ip
        self.protocol = protocol
        self.src_port = src_port
        self.dst_port = dst_port
        self.bytes_count = bytes_count
        self.packets = packets
        self.flags = flags

class TrafficMonitor:
    def __init__(self, config, anomaly_detector):
        self.config = config
        self.anomaly_detector = anomaly_detector
        self.flow_buffer: deque = deque(maxlen=10000)
        self.baseline_traffic: Dict[str, deque] = {}
        self.current_traffic: Dict[str, int] = {}
    
    async def start(self):
        logger.info("Starting sFlow listener", extra={"port": self.config.sflow_port})
        await self._sflow_listener()
    
    async def _sflow_listener(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((self.config.sflow_address, self.config.sflow_port))
        sock.setblocking(False)
        
        loop = asyncio.get_event_loop()
        while True:
            try:
                data, addr = await loop.sock_recvfrom(sock, 65535)
                records = self._parse_sflow(data)
                for record in records:
                    self._process_record(record)
            except Exception as e:
                logger.error("Error processing sFlow packet", extra={"error": str(e)})
    
    def _parse_sflow(self, data):
        records = []
        try:
            offset = 0
            header = struct.unpack_from(">IIII", data, offset)
            offset += 16
            
            if header[0] != 5:
                return records
                
            agent_ip = socket.inet_ntoa(data[offset:offset+4])
            offset += 4
            
            while offset < len(data):
                if offset + 8 > len(data):
                    break
                flow_type, flow_len = struct.unpack_from(">II", data, offset)
                offset += 8
                
                if flow_type == 1:
                    record = self._parse_sample_data(data, offset, flow_len - 8, agent_ip)
                    if record:
                        records.append(record)
                offset += flow_len - 8
        except Exception as e:
            logger.debug("sFlow parse error", extra={"error": str(e)})
        return records
    
    def _parse_sample_data(self, data, offset, length, agent_ip):
        try:
            while offset < length:
                if offset + 8 > len(data):
                    break
                elem_type, elem_len = struct.unpack_from(">II", data, offset)
                offset += 8
                
                if elem_type == 1:
                    return self._parse_flow_sample(data, offset, elem_len - 8)
                offset += elem_len - 8
        except Exception as e:
            logger.debug("Sample parse error", extra={"error": str(e)})
        return None
    
    def _parse_flow_sample(self, data, offset, length):
        try:
            if offset + 40 > len(data):
                return None
                
            src_ip = socket.inet_ntoa(data[offset+12:offset+16])
            dst_ip = socket.inet_ntoa(data[offset+16:offset+20])
            protocol = data[offset+38]
            src_port = struct.unpack_from(">H", data, offset+32)[0]
            dst_port = struct.unpack_from(">H", data, offset+34)[0]
            bytes_count = struct.unpack_from(">I", data, offset+24)[0] * self.config.sampling_rate
            packets = struct.unpack_from(">I", data, offset+20)[0] * self.config.sampling_rate
            flags = data[offset+47] if offset + 47 < len(data) else 0
            
            return TrafficRecord(src_ip, dst_ip, protocol, src_port, dst_port, bytes_count, packets, flags)
        except Exception as e:
            logger.debug("Flow sample parse error", extra={"error": str(e)})
        return None
    
    def _process_record(self, record):
        key = f"{record.dst_ip}"
        self.flow_buffer.append(record)
        self.current_traffic[key] = self.current_traffic.get(key, 0) + record.bytes_count
        
        if key not in self.baseline_traffic:
            self.baseline_traffic[key] = deque(maxlen=self.config.baseline_window // 10)
        
        self.anomaly_detector.analyze(record, self.baseline_traffic, self.current_traffic)
    
    def get_traffic_stats(self):
        return {
            "current_traffic": self.current_traffic.copy(),
            "total_records": len(self.flow_buffer)
        }
