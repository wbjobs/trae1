import random
import hashlib
from typing import Dict, List, Optional, Tuple

from entity import Vector2, InputState, PlayerState, GameWorld
from config import GameConfig


class DeterministicRandom:
    def __init__(self, seed: int = 42):
        self.seed = seed
        self._rng = random.Random(seed)
        self._state_counter = 0

    def reset(self, seed: int = None):
        if seed is not None:
            self.seed = seed
        self._rng = random.Random(self.seed)
        self._state_counter = 0

    def next_int(self, min_val: int, max_val: int) -> int:
        self._state_counter += 1
        return self._rng.randint(min_val, max_val)

    def next_float(self, min_val: float = 0.0, max_val: float = 1.0) -> float:
        self._state_counter += 1
        return self._rng.uniform(min_val, max_val)

    def next_bool(self) -> bool:
        return self.next_int(0, 1) == 1

    def get_state(self) -> Tuple[int, int]:
        return (self.seed, self._state_counter)

    def set_state(self, state: Tuple[int, int]):
        seed, counter = state
        self._rng = random.Random(seed)
        self._state_counter = 0
        for _ in range(counter):
            self._rng.random()
        self._state_counter = counter


class DeterministicPhysics:
    def __init__(self, config: GameConfig):
        self.config = config
        self.rng = DeterministicRandom(seed=42)

    def step(self, world: GameWorld, inputs: Dict[int, InputState], 
             dt: float = None) -> GameWorld:
        if dt is None:
            dt = self.config.FIXED_TIMESTEP

        self._update_players(world, inputs, dt)
        self._update_checksum(world)

        world.frame_number += 1
        return world

    def _update_players(self, world: GameWorld, inputs: Dict[int, InputState], dt: float):
        for player in world.players:
            if not player.is_connected:
                continue

            player_input = inputs.get(player.player_id)
            if player_input is None:
                player_input = InputState()

            move_vec = player_input.to_vector()
            velocity = move_vec * self.config.PLAYER_SPEED

            new_position = player.position + velocity * dt

            radius = self.config.PLAYER_SIZE / 2

            wall_correction = world.check_wall_collision(new_position, radius)
            if wall_correction is not None:
                new_position = wall_correction

            player_correction = world.check_player_collision(
                player.player_id, new_position, radius
            )
            if player_correction is not None:
                new_position = player_correction

            player.position = new_position
            player.velocity = velocity
            player.last_input_sequence = player_input.sequence
            player.predicted_position = player.position
            player.authoritative_position = player.position

    def _update_checksum(self, world: GameWorld):
        checksum_data = f"{world.frame_number}:"
        for player in world.players:
            if player.is_connected:
                checksum_data += f"{player.player_id},{player.position.x},{player.position.y}:"
        
        world.checksum = int(hashlib.md5(checksum_data.encode()).hexdigest(), 16) & 0xFFFFFFFF

    def simulate_frames(self, world: GameWorld, inputs: List[Dict[int, InputState]], 
                        start_frame: int, num_frames: int) -> GameWorld:
        sim_world = GameWorld.from_dict(world.to_dict())
        sim_world.frame_number = start_frame

        for i in range(num_frames):
            if i < len(inputs):
                frame_inputs = inputs[i]
            else:
                frame_inputs = {}
                for p in sim_world.players:
                    if p.is_connected:
                        frame_inputs[p.player_id] = InputState()

            self.step(sim_world, frame_inputs)

        return sim_world

    def predict_player_position(self, player: PlayerState, inputs: List[InputState], 
                                 world: GameWorld, num_frames: int) -> Vector2:
        sim_player = PlayerState.from_dict(player.to_dict())
        
        for input_state in inputs[:num_frames]:
            move_vec = input_state.to_vector()
            velocity = move_vec * self.config.PLAYER_SPEED
            new_pos = sim_player.position + velocity * self.config.FIXED_TIMESTEP
            
            radius = self.config.PLAYER_SIZE / 2
            
            wall_correction = world.check_wall_collision(new_pos, radius)
            if wall_correction is not None:
                new_pos = wall_correction
            
            sim_player.position = new_pos
        
        return sim_player.position

    def get_state_hash(self, world: GameWorld) -> str:
        state_str = f"{world.frame_number}:"
        for player in sorted(world.players, key=lambda p: p.player_id):
            if player.is_connected:
                state_str += f"{player.position.x},{player.position.y}:"
        return hashlib.md5(state_str.encode()).hexdigest()[:16]

    def compare_states(self, world1: GameWorld, world2: GameWorld, 
                       tolerance: float = 0.1) -> Tuple[bool, List[str]]:
        differences = []
        
        if world1.frame_number != world2.frame_number:
            differences.append(f"Frame mismatch: {world1.frame_number} vs {world2.frame_number}")
        
        for p1 in world1.players:
            if not p1.is_connected:
                continue
            p2 = world2.get_player(p1.player_id)
            if p2 is None or not p2.is_connected:
                differences.append(f"Player {p1.player_id} not in second world")
                continue
            
            if abs(p1.position.x - p2.position.x) > tolerance:
                differences.append(f"Player {p1.player_id} X mismatch: {p1.position.x} vs {p2.position.x}")
            if abs(p1.position.y - p2.position.y) > tolerance:
                differences.append(f"Player {p1.player_id} Y mismatch: {p1.position.y} vs {p2.position.y}")
        
        return len(differences) == 0, differences

    def reset(self, seed: int = 42):
        self.rng.reset(seed)
