import struct
import random
from typing import List, Tuple, Optional, Dict


DNP3_PORT = 20000
DNP3_START_BYTE = 0x0564

DNP3_FUNCTION_CODES = {
    0x00: "Confirm",
    0x01: "Read",
    0x02: "Write",
    0x03: "Select",
    0x04: "Operate",
    0x05: "Direct Operate",
    0x06: "Direct Operate No ACK",
    0x07: "Immediate Freeze",
    0x08: "Immediate Freeze No ACK",
    0x09: "Freeze Clear",
    0x0A: "Freeze Clear No ACK",
    0x0B: "Freeze At Time",
    0x0C: "Freeze At Time No ACK",
    0x0D: "Cold Restart",
    0x0E: "Warm Restart",
    0x0F: "Initialize Data",
    0x10: "Initialize Application",
    0x11: "Start Application",
    0x12: "Stop Application",
    0x13: "Save Configuration",
    0x14: "Enable Unsolicited",
    0x15: "Disable Unsolicited",
    0x16: "Assign Class",
    0x17: "Delay Measurement",
    0x18: "Record Current Time",
    0x19: "Open File",
    0x1A: "Close File",
    0x1B: "Delete File",
    0x1C: "Get File Information",
    0x1D: "Authenticate File",
    0x1E: "Abort File",
    0x1F: "Activate Configuration",
    0x20: "Authenticate Request",
    0x21: "Authenticate Error",
    0x22: "Request Key Change",
    0x23: "Key Change",
}

DNP3_INTERNAL_INDICATIONS = {
    0x0001: "All Stations",
    0x0002: "Class 1 Events",
    0x0004: "Class 2 Events",
    0x0008: "Class 3 Events",
    0x0010: "Need Time",
    0x0020: "Local Control",
    0x0040: "Device Trouble",
    0x0080: "Device Restart",
    0x0100: "No Func Code Support",
    0x0200: "Object Unknown",
    0x0400: "Parameter Error",
    0x0800: "Event Buffer Overflow",
    0x1000: "Already Executing",
    0x2000: "Configuration Corrupt",
}

DNP3_FIELD_CONSTRAINTS = {
    'start_byte': {'value': 0x0564, 'mutable': False},
    'length': {'min': 5, 'max': 255, 'mutable': False},
    'destination': {'min': 1, 'max': 65535, 'mutable': False},
    'source': {'min': 1, 'max': 65535, 'mutable': False},
    'dllc': {'mutable': False},
    'dllc_crc': {'mutable': False, 'recalculate': True},
    'transport_header': {'mutable': False},
    'application_data': {'mutable': True},
    'app_function_code': {'min': 0, 'max': 255, 'mutable': True},
    'app_data': {'mutable': True},
}


class DNP3StateMachine:
    def __init__(self):
        self.sequence = 0
        self.fcb = 0
        self.last_seq_received = 0

    def get_next_sequence(self) -> int:
        self.sequence = (self.sequence + 1) % 64
        return self.sequence

    def toggle_fcb(self):
        self.fcb = 1 - self.fcb

    def update_state(self, received_sequence: int):
        self.last_seq_received = received_sequence


class DNP3:
    def __init__(self):
        self.sequence = 0
        self.destination = 1
        self.source = 3
        self.state_machine = DNP3StateMachine()

    @staticmethod
    def crc16(data: bytes) -> int:
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 0x0001:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc

    @staticmethod
    def parse_header(data: bytes) -> Optional[dict]:
        if len(data) < 10:
            return None
        
        start_byte = struct.unpack('>H', data[:2])[0]
        if start_byte != DNP3_START_BYTE:
            return None
        
        length = struct.unpack('<H', data[2:4])[0]
        if length > 255 or length < 5:
            return None
        
        destination = struct.unpack('<H', data[4:6])[0]
        source = struct.unpack('<H', data[6:8])[0]
        
        dllc = data[8]
        primary = ((dllc & 0x10) == 0)
        fcb = (dllc & 0x20) >> 5
        fcv_dfc = (dllc & 0x10) >> 4
        function_code = dllc & 0x0F
        
        return {
            'start_byte': start_byte,
            'length': length,
            'destination': destination,
            'source': source,
            'dllc': dllc,
            'primary': primary,
            'fcb': fcb,
            'fcv_dfc': fcv_dfc,
            'function_code': function_code,
            'header_size': 10
        }

    @staticmethod
    def parse_transport_header(data: bytes, offset: int = 10) -> Optional[dict]:
        if len(data) <= offset:
            return None
        th = data[offset]
        fin = (th & 0x80) >> 7
        fir = (th & 0x40) >> 6
        sequence = th & 0x3F
        return {
            'fin': fin,
            'fir': fir,
            'sequence': sequence,
            'header_size': 1
        }

    @staticmethod
    def parse_application_layer(data: bytes, offset: int) -> Optional[dict]:
        if len(data) <= offset + 4:
            return None
        
        app_control = data[offset]
        app_function = data[offset + 1]
        
        result = {
            'app_control': app_control,
            'app_function': app_function,
            'app_function_name': DNP3_FUNCTION_CODES.get(app_function, f"Unknown ({app_function})"),
            'header_size': 2
        }
        
        if len(data) >= offset + 4:
            internal_indications = struct.unpack('<H', data[offset + 2:offset + 4])[0]
            result.update({
                'internal_indications': internal_indications,
                'indications': DNP3.decode_internal_indications(internal_indications),
                'header_size': 4,
                'data': data[offset + 4:]
            })
        
        return result

    def build_request(self, destination: int = None, source: int = None, 
                      function_code: int = None, data: bytes = None) -> bytes:
        if destination is None:
            destination = self.destination
        if source is None:
            source = self.source
        if function_code is None:
            function_code = random.choice([0x01, 0x02, 0x05, 0x0D])
        if data is None:
            data = bytes([0xC4, 0x01, 0x00, 0x00])
        
        seq = self.state_machine.get_next_sequence()
        
        tp_header = struct.pack('B', 0xC0 | (seq & 0x3F))
        
        application_data = tp_header + data
        
        length = len(application_data) + 5
        
        dllc = 0xC0 | (function_code & 0x0F)
        
        header = struct.pack('<HHHHB', DNP3_START_BYTE, length, destination, source, dllc)
        
        dllc_crc = struct.pack('<H', DNP3.crc16(struct.pack('B', dllc)))
        
        return header + dllc_crc + application_data

    @staticmethod
    def parse_full_packet(data: bytes) -> Optional[dict]:
        dll_header = DNP3.parse_header(data)
        if not dll_header:
            return None
        
        offset = dll_header['header_size']
        
        dllc_crc_data = data[offset:offset + 2]
        expected_crc = struct.unpack('<H', dllc_crc_data)[0]
        calculated_crc = DNP3.crc16(struct.pack('B', dll_header['dllc']))
        
        offset += 2
        
        tp_header = DNP3.parse_transport_header(data, offset)
        if not tp_header:
            return None
        
        offset += tp_header['header_size']
        
        app_layer = DNP3.parse_application_layer(data, offset)
        
        result = {
            **dll_header,
            'dllc_crc_valid': expected_crc == calculated_crc,
            'dllc_crc_expected': expected_crc,
            'dllc_crc_calculated': calculated_crc,
            **tp_header,
        }
        
        if app_layer:
            result.update(app_layer)
            result['app_data'] = result.get('data', b'')
        
        return result

    @staticmethod
    def parse_response(data: bytes) -> Optional[dict]:
        return DNP3.parse_full_packet(data)

    @staticmethod
    def decode_internal_indications(indications: int) -> List[str]:
        decoded = []
        for bit, name in DNP3_INTERNAL_INDICATIONS.items():
            if indications & bit:
                decoded.append(name)
        return decoded

    @staticmethod
    def is_exception_response(data: bytes) -> Tuple[bool, Optional[str]]:
        parsed = DNP3.parse_response(data)
        if not parsed:
            return False, None
        
        if 'indications' in parsed:
            if 'Device Trouble' in parsed['indications']:
                return True, 'Device Trouble'
            if 'Device Restart' in parsed['indications']:
                return True, 'Device Restart'
            if 'No Func Code Support' in parsed['indications']:
                return True, 'No Func Code Support'
            if 'Object Unknown' in parsed['indications']:
                return True, 'Object Unknown'
            if 'Parameter Error' in parsed['indications']:
                return True, 'Parameter Error'
            if 'Configuration Corrupt' in parsed['indications']:
                return True, 'Configuration Corrupt'
            if 'Event Buffer Overflow' in parsed['indications']:
                return True, 'Event Buffer Overflow'
        
        return False, None

    @staticmethod
    def extract_packets_from_pcap(packets) -> List[bytes]:
        seed_packets = []
        try:
            from scapy.all import TCP, UDP, Raw
            for pkt in packets:
                if (TCP in pkt or UDP in pkt) and Raw in pkt:
                    sport = pkt[TCP].sport if TCP in pkt else pkt[UDP].sport
                    dport = pkt[TCP].dport if TCP in pkt else pkt[UDP].dport
                    if sport == DNP3_PORT or dport == DNP3_PORT:
                        payload = bytes(pkt[Raw].load)
                        if len(payload) >= 10:
                            header = DNP3.parse_header(payload)
                            if header:
                                if dport == DNP3_PORT:
                                    seed_packets.append(payload)
        except ImportError:
            pass
        return seed_packets

    @staticmethod
    def generate_sample_packets() -> List[bytes]:
        samples = []
        for destination in [1, 1024]:
            for source in [3, 100]:
                for func_code in [0x01, 0x02, 0x05, 0x0D, 0x0E, 0x14, 0x15]:
                    if func_code == 0x01:
                        data = bytes([0xC4, 0x01, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x07, 0x00])
                    elif func_code == 0x02:
                        data = bytes([0xC4, 0x02, 0x00, 0x00, 0x28, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00])
                    elif func_code == 0x05:
                        data = bytes([0xC4, 0x05, 0x00, 0x00, 0x0A, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x81, 0x00, 0x01])
                    elif func_code in [0x0D, 0x0E]:
                        data = bytes([0xC4, func_code, 0x00, 0x00])
                    elif func_code in [0x14, 0x15]:
                        data = bytes([0xC4, func_code, 0x00, 0x00, 0x3C, 0x02, 0x07, 0x01, 0x00, 0x3C, 0x02, 0x07, 0x02, 0x00, 0x3C, 0x02, 0x07, 0x03, 0x00])
                    else:
                        data = b''
                    
                    dnp3 = DNP3()
                    dnp3.destination = destination
                    dnp3.source = source
                    samples.append(dnp3.build_request(destination, source, func_code, data))
        return samples

    @staticmethod
    def validate_fields(parsed: dict) -> Tuple[bool, str]:
        if not parsed:
            return False, "Empty packet"
        
        if parsed.get('start_byte') != DNP3_START_BYTE:
            return False, f"Invalid start byte: {parsed.get('start_byte')}"
        
        length = parsed.get('length', 0)
        if not (5 <= length <= 255):
            return False, f"Invalid length: {length}"
        
        if not parsed.get('dllc_crc_valid', False):
            return False, "DLLC CRC validation failed"
        
        return True, "Valid"

    @staticmethod
    def rebuild_valid_packet(original_data: bytes, 
                            new_destination: int = None,
                            new_source: int = None,
                            new_app_function: int = None,
                            new_app_data: bytes = None,
                            new_sequence: int = None) -> bytes:
        parsed = DNP3.parse_full_packet(original_data)
        if not parsed:
            dnp3 = DNP3()
            return dnp3.build_request()
        
        destination = new_destination if new_destination is not None else parsed['destination']
        source = new_source if new_source is not None else parsed['source']
        
        dllc = parsed.get('dllc', 0xC0)
        
        if new_sequence is None:
            tp_header_val = 0xC0 | (parsed.get('sequence', 0) & 0x3F)
        else:
            tp_header_val = 0xC0 | (new_sequence & 0x3F)
        
        app_control = parsed.get('app_control', 0xC4)
        
        if new_app_function is not None:
            app_function = new_app_function & 0xFF
        else:
            app_function = parsed.get('app_function', 0x01)
        
        if new_app_data is not None:
            app_data = new_app_data
        else:
            app_data = parsed.get('app_data', b'')
        
        application_layer = struct.pack('BB', app_control, app_function)
        
        if 'internal_indications' in parsed:
            ii = parsed.get('internal_indications', 0)
            application_layer += struct.pack('<H', ii)
        
        application_layer += app_data
        
        tp_header_bytes = struct.pack('B', tp_header_val)
        
        application_data = tp_header_bytes + application_layer
        
        length = len(application_data) + 5
        
        header = struct.pack('<HHHHB', DNP3_START_BYTE, length, destination, source, dllc)
        
        dllc_crc = struct.pack('<H', DNP3.crc16(struct.pack('B', dllc)))
        
        return header + dllc_crc + application_data

    @staticmethod
    def smart_mutate(data: bytes, mutation_type: str, mutator) -> Tuple[bytes, str]:
        parsed = DNP3.parse_full_packet(data)
        if not parsed:
            mutated_data = mutator.mutate(data)
            return mutated_data, mutation_type
        
        original_app_func = parsed.get('app_function', 0x01)
        original_app_data = parsed.get('app_data', b'')
        
        if mutation_type == 'bit_flip':
            if len(original_app_data) > 0:
                mutated_data_bytes = list(original_app_data)
                num_flips = random.randint(1, max(1, len(mutated_data_bytes)))
                for _ in range(num_flips):
                    idx = random.randint(0, len(mutated_data_bytes) - 1)
                    bit_idx = random.randint(0, 7)
                    mutated_data_bytes[idx] ^= (1 << bit_idx)
                new_app_data = bytes(mutated_data_bytes)
            else:
                new_app_data = original_app_data
            
            new_app_func = original_app_func
            if random.random() < 0.1:
                new_app_func = original_app_func ^ (1 << random.randint(0, 7))
                new_app_func = new_app_func & 0xFF
                
        elif mutation_type == 'boundary':
            boundary_values = [0x00, 0xFF, 0x7F, 0x80, 0x01, 0xFE]
            if len(original_app_data) > 0:
                data_list = list(original_app_data)
                num_changes = random.randint(1, max(1, len(data_list) // 2))
                for _ in range(num_changes):
                    idx = random.randint(0, len(data_list) - 1)
                    data_list[idx] = random.choice(boundary_values)
                new_app_data = bytes(data_list)
            else:
                new_app_data = bytes([random.choice(boundary_values)])
            
            if random.random() < 0.15:
                new_app_func = random.choice(boundary_values)
            else:
                new_app_func = original_app_func
                
        elif mutation_type == 'random_byte':
            if len(original_app_data) > 0:
                data_list = list(original_app_data)
                num_changes = random.randint(1, max(1, len(data_list) // 2))
                for _ in range(num_changes):
                    idx = random.randint(0, len(data_list) - 1)
                    data_list[idx] = random.randint(0, 255)
                new_app_data = bytes(data_list)
            else:
                new_app_data = bytes([random.randint(0, 255) for _ in range(random.randint(2, 10))])
            
            if random.random() < 0.2:
                new_app_func = random.randint(0, 255)
            else:
                new_app_func = original_app_func
                
        elif mutation_type == 'length_overflow':
            overflow_size = random.randint(1, 50)
            overflow_data = bytes([random.randint(0, 255) for _ in range(overflow_size)])
            
            if random.random() < 0.5:
                new_app_data = original_app_data + overflow_data
            else:
                insert_pos = random.randint(0, len(original_app_data))
                new_app_data = original_app_data[:insert_pos] + overflow_data + original_app_data[insert_pos:]
            
            new_app_func = original_app_func
            
        else:
            new_app_data = mutator.mutate(original_app_data)
            new_app_func = original_app_func
        
        if random.random() < 0.05:
            valid_funcs = list(DNP3_FUNCTION_CODES.keys())
            new_app_func = random.choice(valid_funcs)
        
        max_data_len = 240
        if len(new_app_data) > max_data_len:
            new_app_data = new_app_data[:max_data_len]
        
        result = DNP3.rebuild_valid_packet(
            data,
            new_app_function=new_app_func,
            new_app_data=new_app_data
        )
        
        return result, mutation_type

    @staticmethod
    def get_field_constraints() -> Dict:
        return DNP3_FIELD_CONSTRAINTS

    @staticmethod
    def get_protocol_info() -> dict:
        return {
            'name': 'DNP3',
            'port': DNP3_PORT,
            'function_codes': DNP3_FUNCTION_CODES,
            'field_constraints': DNP3_FIELD_CONSTRAINTS,
            'stateful': True,
        }
