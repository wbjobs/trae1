import struct
import random
from typing import List, Tuple, Optional, Dict


MODBUS_TCP_PORT = 502
MODBUS_FUNCTION_CODES = {
    0x01: "Read Coils",
    0x02: "Read Discrete Inputs",
    0x03: "Read Holding Registers",
    0x04: "Read Input Registers",
    0x05: "Write Single Coil",
    0x06: "Write Single Register",
    0x0F: "Write Multiple Coils",
    0x10: "Write Multiple Registers",
}

MODBUS_EXCEPTION_CODES = {
    0x01: "Illegal Function",
    0x02: "Illegal Data Address",
    0x03: "Illegal Data Value",
    0x04: "Slave Device Failure",
    0x05: "Acknowledge",
    0x06: "Slave Device Busy",
}

MODBUS_FIELD_CONSTRAINTS = {
    'transaction_id': {'min': 0x0000, 'max': 0xFFFF, 'mutable': False},
    'protocol_id': {'min': 0x0000, 'max': 0x0000, 'mutable': False},
    'length': {'min': 2, 'max': 253, 'mutable': False},
    'unit_id': {'min': 0x01, 'max': 0xF7, 'mutable': False},
    'function_code': {'min': 0x01, 'max': 0x7F, 'mutable': True},
    'data': {'mutable': True},
}


class ModbusTCP:
    def __init__(self):
        self.transaction_id = 0
        self.unit_id = 1

    @staticmethod
    def build_mbap_header(transaction_id: int, protocol_id: int, length: int, unit_id: int) -> bytes:
        return struct.pack('>HHHB', transaction_id, protocol_id, length, unit_id)

    @staticmethod
    def parse_mbap_header(data: bytes) -> Optional[dict]:
        if len(data) < 7:
            return None
        transaction_id, protocol_id, length, unit_id = struct.unpack('>HHHB', data[:7])
        return {
            'transaction_id': transaction_id,
            'protocol_id': protocol_id,
            'length': length,
            'unit_id': unit_id,
            'header_size': 7
        }

    def build_request(self, unit_id: int = None, function_code: int = None, data: bytes = None) -> bytes:
        if unit_id is None:
            unit_id = self.unit_id
        if function_code is None:
            function_code = random.choice(list(MODBUS_FUNCTION_CODES.keys()))
        if data is None:
            data = bytes([random.randint(0, 255) for _ in range(random.randint(2, 10))])
        
        self.transaction_id = (self.transaction_id + 1) % 65536
        length = len(data) + 2
        header = self.build_mbap_header(self.transaction_id, 0, length, unit_id)
        pdu = struct.pack('>B', function_code) + data
        return header + pdu

    @staticmethod
    def parse_request(data: bytes) -> Optional[dict]:
        if len(data) < 8:
            return None
        
        mbap = ModbusTCP.parse_mbap_header(data)
        if not mbap:
            return None
        
        function_code = data[7]
        pdu_data = data[8:]
        
        return {
            **mbap,
            'function_code': function_code,
            'data': pdu_data,
        }

    @staticmethod
    def parse_response(data: bytes) -> Optional[dict]:
        mbap = ModbusTCP.parse_mbap_header(data)
        if not mbap:
            return None
        
        if len(data) < 7 + 2:
            return None
        
        function_code = data[7]
        is_exception = function_code > 0x80
        
        result = {
            **mbap,
            'function_code': function_code & 0x7F,
            'is_exception': is_exception,
        }
        
        if is_exception:
            if len(data) >= 9:
                result['exception_code'] = data[8]
                result['exception_name'] = MODBUS_EXCEPTION_CODES.get(data[8], f"Unknown ({data[8]})")
        else:
            result['data'] = data[8:]
        
        return result

    @staticmethod
    def is_exception_response(data: bytes) -> Tuple[bool, Optional[str]]:
        parsed = ModbusTCP.parse_response(data)
        if not parsed:
            return False, None
        if parsed.get('is_exception', False):
            return True, parsed.get('exception_name', 'Unknown exception')
        return False, None

    @staticmethod
    def extract_packets_from_pcap(packets) -> List[bytes]:
        seed_packets = []
        try:
            from scapy.all import TCP, Raw
            for pkt in packets:
                if TCP in pkt and Raw in pkt:
                    if pkt[TCP].dport == MODBUS_TCP_PORT or pkt[TCP].sport == MODBUS_TCP_PORT:
                        payload = bytes(pkt[Raw].load)
                        if len(payload) >= 8:
                            mbap = ModbusTCP.parse_mbap_header(payload)
                            if mbap and mbap['protocol_id'] == 0:
                                if pkt[TCP].dport == MODBUS_TCP_PORT:
                                    seed_packets.append(payload)
        except ImportError:
            pass
        return seed_packets

    @staticmethod
    def generate_sample_packets() -> List[bytes]:
        samples = []
        for unit_id in [1, 2, 10]:
            for func_code in [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x0F, 0x10]:
                if func_code in [0x01, 0x02, 0x03, 0x04]:
                    data = struct.pack('>HH', 0, 10)
                elif func_code == 0x05:
                    data = struct.pack('>HH', 0, 0xFF00)
                elif func_code == 0x06:
                    data = struct.pack('>HH', 0, 1234)
                elif func_code == 0x0F:
                    data = struct.pack('>HHB', 0, 8, 1) + bytes([0xFF])
                elif func_code == 0x10:
                    data = struct.pack('>HHB', 0, 2, 4) + struct.pack('>HH', 1, 2)
                else:
                    data = b'\x00\x00\x00\x00'
                
                mb = ModbusTCP()
                mb.unit_id = unit_id
                samples.append(mb.build_request(unit_id, func_code, data))
        return samples

    @staticmethod
    def validate_fields(parsed: dict) -> Tuple[bool, str]:
        if not parsed:
            return False, "Empty packet"
        
        constraints = MODBUS_FIELD_CONSTRAINTS
        
        tid = parsed.get('transaction_id', 0)
        if not (constraints['transaction_id']['min'] <= tid <= constraints['transaction_id']['max']):
            return False, f"Invalid Transaction ID: {tid}"
        
        if parsed.get('protocol_id', 0) != 0:
            return False, f"Invalid Protocol ID: {parsed.get('protocol_id')}"
        
        uid = parsed.get('unit_id', 0)
        if not (constraints['unit_id']['min'] <= uid <= constraints['unit_id']['max']):
            return False, f"Invalid Unit ID: {uid} (must be 1-247)"
        
        fc = parsed.get('function_code', 0)
        if fc < 1 or fc > 127:
            return False, f"Invalid Function Code: {fc}"
        
        expected_length = len(parsed.get('data', b'')) + 2
        if parsed.get('length', 0) != expected_length:
            return False, f"Length mismatch: {parsed.get('length')} != {expected_length}"
        
        return True, "Valid"

    @staticmethod
    def rebuild_valid_packet(original_data: bytes, new_transaction_id: int = None, 
                            new_unit_id: int = None, new_function_code: int = None, 
                            new_data: bytes = None) -> bytes:
        parsed = ModbusTCP.parse_request(original_data)
        if not parsed:
            mb = ModbusTCP()
            return mb.build_request()
        
        tid = new_transaction_id if new_transaction_id is not None else parsed['transaction_id']
        uid = new_unit_id if new_unit_id is not None else parsed['unit_id']
        fc = new_function_code if new_function_code is not None else parsed['function_code']
        data = new_data if new_data is not None else parsed['data']
        
        uid = max(MODBUS_FIELD_CONSTRAINTS['unit_id']['min'], 
                  min(MODBUS_FIELD_CONSTRAINTS['unit_id']['max'], uid))
        
        if fc > 0x7F:
            fc = fc & 0x7F
        
        length = len(data) + 2
        
        header = ModbusTCP.build_mbap_header(tid, 0, length, uid)
        pdu = struct.pack('>B', fc) + data
        return header + pdu

    @staticmethod
    def smart_mutate(data: bytes, mutation_type: str, mutator) -> Tuple[bytes, str]:
        parsed = ModbusTCP.parse_request(data)
        if not parsed:
            mutated_data = mutator.mutate(data)
            return mutated_data, mutation_type
        
        original_fc = parsed['function_code']
        original_data = parsed['data']
        
        if mutation_type == 'bit_flip':
            if random.random() < 0.3 and len(original_data) > 0:
                mutated_data_bytes = list(original_data)
                num_flips = random.randint(1, max(1, len(mutated_data_bytes)))
                for _ in range(num_flips):
                    idx = random.randint(0, len(mutated_data_bytes) - 1)
                    bit_idx = random.randint(0, 7)
                    mutated_data_bytes[idx] ^= (1 << bit_idx)
                new_data = bytes(mutated_data_bytes)
            else:
                new_data = mutator.bit_flip(original_data)
            
            new_fc = original_fc
            if random.random() < 0.1:
                new_fc = original_fc ^ (1 << random.randint(0, 6))
                new_fc = max(1, min(127, new_fc & 0x7F))
                
        elif mutation_type == 'boundary':
            boundary_values = [0x00, 0xFF, 0x7F, 0x80, 0x01, 0xFE, 0x7F, 0x80]
            if len(original_data) > 0:
                data_list = list(original_data)
                num_changes = random.randint(1, max(1, len(data_list) // 2))
                for _ in range(num_changes):
                    idx = random.randint(0, len(data_list) - 1)
                    data_list[idx] = random.choice(boundary_values)
                new_data = bytes(data_list)
            else:
                new_data = original_data
            
            if random.random() < 0.15:
                new_fc = random.choice(boundary_values)
                new_fc = max(1, min(127, new_fc & 0x7F))
            else:
                new_fc = original_fc
                
        elif mutation_type == 'random_byte':
            if len(original_data) > 0:
                data_list = list(original_data)
                num_changes = random.randint(1, max(1, len(data_list) // 2))
                for _ in range(num_changes):
                    idx = random.randint(0, len(data_list) - 1)
                    data_list[idx] = random.randint(0, 255)
                new_data = bytes(data_list)
            else:
                new_data = bytes([random.randint(0, 255) for _ in range(random.randint(2, 8))])
            
            if random.random() < 0.2:
                new_fc = random.randint(1, 127)
            else:
                new_fc = original_fc
                
        elif mutation_type == 'length_overflow':
            overflow_size = random.randint(1, 50)
            overflow_data = bytes([random.randint(0, 255) for _ in range(overflow_size)])
            
            if random.random() < 0.5:
                new_data = original_data + overflow_data
            else:
                insert_pos = random.randint(0, len(original_data))
                new_data = original_data[:insert_pos] + overflow_data + original_data[insert_pos:]
            
            new_fc = original_fc
            
        else:
            new_data = mutator.mutate(original_data)
            new_fc = original_fc
        
        if random.random() < 0.05:
            valid_fcs = list(MODBUS_FUNCTION_CODES.keys())
            new_fc = random.choice(valid_fcs)
        
        new_data = new_data[:251]
        
        result = ModbusTCP.rebuild_valid_packet(
            data,
            new_function_code=new_fc,
            new_data=new_data
        )
        
        return result, mutation_type

    @staticmethod
    def get_field_constraints() -> Dict:
        return MODBUS_FIELD_CONSTRAINTS

    @staticmethod
    def get_protocol_info() -> dict:
        return {
            'name': 'Modbus TCP',
            'port': MODBUS_TCP_PORT,
            'function_codes': MODBUS_FUNCTION_CODES,
            'field_constraints': MODBUS_FIELD_CONSTRAINTS,
            'stateful': False,
        }
