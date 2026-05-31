import random
import struct


class Mutator:
    def __init__(self, mutation_weights=None):
        self.mutation_weights = mutation_weights or {
            'bit_flip': 1.0,
            'boundary': 1.0,
            'random_byte': 1.0,
            'length_overflow': 1.0
        }

    def mutate(self, data: bytes) -> bytes:
        mutations = list(self.mutation_weights.keys())
        weights = list(self.mutation_weights.values())
        mutation_type = random.choices(mutations, weights=weights, k=1)[0]
        
        mutation_methods = {
            'bit_flip': self.bit_flip,
            'boundary': self.boundary_value,
            'random_byte': self.random_byte,
            'length_overflow': self.length_overflow
        }
        
        return mutation_methods[mutation_type](data)

    def bit_flip(self, data: bytes, num_flips: int = None) -> bytes:
        if not data:
            return data
        
        data_list = list(data)
        num_flips = num_flips or random.randint(1, max(1, len(data) * 2))
        
        for _ in range(num_flips):
            byte_idx = random.randint(0, len(data_list) - 1)
            bit_idx = random.randint(0, 7)
            data_list[byte_idx] ^= (1 << bit_idx)
        
        return bytes(data_list)

    def boundary_value(self, data: bytes) -> bytes:
        if len(data) < 1:
            return data
        
        data_list = list(data)
        boundary_values = [0x00, 0xFF, 0x7F, 0x80, 0x01, 0xFE]
        
        num_changes = random.randint(1, max(1, len(data_list) // 4))
        for _ in range(num_changes):
            idx = random.randint(0, len(data_list) - 1)
            data_list[idx] = random.choice(boundary_values)
        
        return bytes(data_list)

    def random_byte(self, data: bytes) -> bytes:
        if len(data) < 1:
            return data
        
        data_list = list(data)
        num_changes = random.randint(1, max(1, len(data_list) // 2))
        
        for _ in range(num_changes):
            idx = random.randint(0, len(data_list) - 1)
            data_list[idx] = random.randint(0, 255)
        
        return bytes(data_list)

    def length_overflow(self, data: bytes) -> bytes:
        overflow_size = random.randint(1, 1024)
        overflow_data = bytes([random.randint(0, 255) for _ in range(overflow_size)])
        
        if random.random() < 0.5:
            return data + overflow_data
        else:
            insert_pos = random.randint(0, len(data))
            return data[:insert_pos] + overflow_data + data[insert_pos:]

    def update_weights(self, successful_mutations: dict):
        total = sum(successful_mutations.values())
        if total == 0:
            return
        
        for mutation, count in successful_mutations.items():
            if mutation in self.mutation_weights:
                self.mutation_weights[mutation] = 1.0 + (count / total) * 2.0
