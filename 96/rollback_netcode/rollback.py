import copy
import logging
from typing import Dict, List, Optional, Tuple
from collections import deque

from entity import Vector2, InputState, PlayerState, GameWorld
from physics import DeterministicPhysics
from config import GameConfig


class RollbackManager:
    def __init__(self, config: GameConfig, physics: DeterministicPhysics):
        self.config = config
        self.physics = physics
        self.logger = logging.getLogger("RollbackManager")
        
        self.input_buffer: Dict[int, deque] = {}
        self.state_history: List[GameWorld] = []
        self.max_history_size = config.MAX_ROLLBACK_FRAMES * 2
        
        self.current_frame = 0
        self.confirmed_frame = 0
        self.predicted_frame = 0
        
        self.rollback_count = 0
        self.total_rollback_frames = 0
        self.last_rollback_frame = -1

    def initialize(self, initial_world: GameWorld):
        self.state_history = [initial_world]
        self.current_frame = 0
        self.confirmed_frame = 0
        self.predicted_frame = 0
        self.rollback_count = 0
        self.total_rollback_frames = 0
        self.input_buffer = {}

    def add_input(self, player_id: int, input_state: InputState):
        if player_id not in self.input_buffer:
            self.input_buffer[player_id] = deque(maxlen=self.config.MAX_ROLLBACK_FRAMES * 2)
        
        input_state.sequence = len(self.input_buffer[player_id])
        self.input_buffer[player_id].append(input_state)
        
        while len(self.input_buffer[player_id]) > self.config.MAX_ROLLBACK_FRAMES * 2:
            self.input_buffer[player_id].popleft()

    def get_input_for_frame(self, player_id: int, frame: int) -> InputState:
        if player_id not in self.input_buffer:
            return InputState()
        
        buffer = self.input_buffer[player_id]
        if frame < len(buffer):
            return buffer[frame]
        
        return InputState()

    def get_frame_inputs(self, frame: int) -> Dict[int, InputState]:
        inputs = {}
        for player_id in self.input_buffer:
            inputs[player_id] = self.get_input_for_frame(player_id, frame)
        return inputs

    def save_state(self, world: GameWorld):
        state_copy = GameWorld.from_dict(world.to_dict())
        self.state_history.append(state_copy)
        
        while len(self.state_history) > self.max_history_size:
            self.state_history.pop(0)

    def get_state_at_frame(self, frame: int) -> Optional[GameWorld]:
        if not self.state_history:
            return None
        
        relative_frame = frame - self.state_history[0].frame_number
        
        if relative_frame < 0 or relative_frame >= len(self.state_history):
            return None
        
        return self.state_history[relative_frame]

    def rollback_to_frame(self, target_frame: int) -> Optional[GameWorld]:
        if target_frame < self.confirmed_frame:
            self.logger.warning(f"Cannot rollback to frame {target_frame}, below confirmed frame {self.confirmed_frame}")
            return None
        
        target_state = self.get_state_at_frame(target_frame)
        if target_state is None:
            self.logger.warning(f"No state found for frame {target_frame}")
            return None
        
        if target_state.frame_number >= self.current_frame:
            return target_state
        
        self.rollback_count += 1
        frames_rolled_back = self.current_frame - target_state.frame_number
        self.total_rollback_frames += frames_rolled_back
        self.last_rollback_frame = self.current_frame
        
        self.logger.info(
            f"ROLLBACK: From frame {self.current_frame} to {target_state.frame_number} "
            f"({frames_rolled_back} frames)"
        )
        
        self.current_frame = target_state.frame_number
        self.predicted_frame = target_state.frame_number
        
        history_index = target_state.frame_number - self.state_history[0].frame_number
        if history_index >= 0 and history_index < len(self.state_history):
            self.state_history = self.state_history[:history_index + 1]
        
        return GameWorld.from_dict(target_state.to_dict())

    def resimulate_frames(self, start_world: GameWorld, start_frame: int, 
                          end_frame: int) -> GameWorld:
        world = start_world
        world.frame_number = start_frame
        
        for frame in range(start_frame, end_frame):
            inputs = self.get_frame_inputs(frame)
            self.physics.step(world, inputs)
            self.save_state(world)
            self.current_frame = frame + 1
            self.predicted_frame = frame + 1
        
        return world

    def predict_next_frames(self, current_world: GameWorld, num_frames: int,
                            player_id: int, future_inputs: List[InputState]) -> List[Vector2]:
        predicted_positions = []
        sim_world = GameWorld.from_dict(current_world.to_dict())
        
        for i in range(num_frames):
            if i < len(future_inputs):
                inputs = {player_id: future_inputs[i]}
            else:
                inputs = {player_id: InputState()}
            
            self.physics.step(sim_world, inputs)
            
            player = sim_world.get_player(player_id)
            if player:
                predicted_positions.append(player.position.clone())
        
        return predicted_positions

    def apply_correction(self, player_id: int, corrected_position: Vector2, 
                         corrected_frame: int) -> Tuple[bool, int]:
        current_world = self.get_state_at_frame(self.current_frame)
        if current_world is None:
            return False, 0
        
        current_player = current_world.get_player(player_id)
        if current_player is None:
            return False, 0
        
        distance = current_player.position.distance_to(corrected_position)
        
        if distance < 0.5:
            current_player.authoritative_position = corrected_position
            self.save_state(current_world)
            return False, 0
        
        self.logger.info(
            f"CORRECTION NEEDED for player {player_id} at frame {corrected_frame}: "
            f"distance={distance:.2f}, predicted={current_player.position}, "
            f"authoritative={corrected_position}"
        )
        
        rollback_world = self.rollback_to_frame(corrected_frame)
        if rollback_world is None:
            current_player.authoritative_position = corrected_position
            self._smooth_correct_position(current_player, corrected_position)
            return False, 0
        
        corrected_player = rollback_world.get_player(player_id)
        if corrected_player:
            corrected_player.position = corrected_position.clone()
            corrected_player.authoritative_position = corrected_position.clone()
        
        resimulated = self.resimulate_frames(
            rollback_world, corrected_frame, self.confirmed_frame
        )
        
        new_player = resimulated.get_player(player_id)
        if new_player:
            self.logger.info(
                f"ROLLBACK COMPLETE: player {player_id} now at "
                f"{new_player.position} (was {current_player.position})"
            )
        
        return True, self.current_frame - corrected_frame

    def _smooth_correct_position(self, player: PlayerState, target: Vector2):
        player.position = player.position.lerp(target, self.config.CORRECTION_SMOOTHING)
        player.authoritative_position = target.clone()

    def confirm_frame(self, frame_number: int):
        if frame_number > self.confirmed_frame:
            self.confirmed_frame = frame_number
        
        while len(self.state_history) > 0 and self.state_history[0].frame_number < self.confirmed_frame - self.config.MAX_ROLLBACK_FRAMES:
            self.state_history.pop(0)
        
        for player_id, buffer in self.input_buffer.items():
            while len(buffer) > 0 and buffer[0].sequence < self.confirmed_frame:
                buffer.popleft()

    def get_statistics(self) -> dict:
        return {
            'rollback_count': self.rollback_count,
            'total_rollback_frames': self.total_rollback_frames,
            'last_rollback_frame': self.last_rollback_frame,
            'current_frame': self.current_frame,
            'confirmed_frame': self.confirmed_frame,
            'predicted_frame': self.predicted_frame,
            'history_size': len(self.state_history),
            'avg_rollback_frames': (
                self.total_rollback_frames / self.rollback_count 
                if self.rollback_count > 0 else 0
            )
        }

    def check_prediction_error(self, player_id: int, authoritative_position: Vector2,
                                frame_number: int) -> Tuple[bool, float]:
        predicted_state = self.get_state_at_frame(frame_number)
        if predicted_state is None:
            return False, 0.0
        
        predicted_player = predicted_state.get_player(player_id)
        if predicted_player is None:
            return False, 0.0
        
        distance = predicted_player.position.distance_to(authoritative_position)
        has_error = distance > 1.0
        
        return has_error, distance

    def clear_old_history(self):
        cutoff_frame = self.confirmed_frame - self.config.MAX_ROLLBACK_FRAMES
        
        while len(self.state_history) > 0 and self.state_history[0].frame_number < cutoff_frame:
            self.state_history.pop(0)

    def reset(self):
        self.state_history.clear()
        self.input_buffer.clear()
        self.current_frame = 0
        self.confirmed_frame = 0
        self.predicted_frame = 0
        self.rollback_count = 0
        self.total_rollback_frames = 0
        self.last_rollback_frame = -1
