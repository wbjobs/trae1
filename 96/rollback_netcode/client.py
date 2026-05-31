import logging
import socket
import threading
import time
from typing import Dict, List, Optional, Set

from entity import Vector2, InputState, PlayerState, GameWorld
from physics import DeterministicPhysics
from rollback import RollbackManager
from network import BaseNetwork, Connection, NetworkPacket
from config import GameConfig, DEFAULT_CONFIG


class GameClient(BaseNetwork):
    def __init__(self, config: GameConfig = DEFAULT_CONFIG, host: str = '127.0.0.1'):
        super().__init__(config)
        self.host = host
        self.port = config.NETWORK_PORT
        
        self.logger = logging.getLogger("GameClient")
        self.logger.setLevel(getattr(logging, config.LOG_LEVEL))
        
        self.world: Optional[GameWorld] = None
        self.physics = DeterministicPhysics(config)
        self.rollback = RollbackManager(config, self.physics)
        
        self.connection: Optional[Connection] = None
        self.player_id: int = -1
        self.connected = False
        
        self.predicted_world: Optional[GameWorld] = None
        self.interpolated_positions: Dict[int, Vector2] = {}
        
        self.pending_corrections: List[Tuple[int, Vector2]] = []
        self.acknowledged_frames: Set[int] = set()
        
        self.current_input = InputState()
        self.input_sequence = 0
        
        self._network_thread: Optional[threading.Thread] = None
        self._game_thread: Optional[threading.Thread] = None
        
        self.prediction_window_frames = config.max_prediction_frames
        self.last_server_state_frame = -1
        self.average_ping = 0.0
        
        self.debug_info = {
            'prediction_errors': 0,
            'rollback_count': 0,
            'total_corrections': 0,
            'smooth_corrections': 0,
        }

    def connect(self) -> bool:
        self.logger.info(f"Connecting to server at {self.host}:{self.port}")
        
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5.0)
            sock.connect((self.host, self.port))
            sock.settimeout(0.1)
            
            self.connection = Connection(sock, (self.host, self.port), -1)
            
            welcome_timeout = time.time() + 5.0
            while time.time() < welcome_timeout and not self.connected:
                packets = self.connection.receive()
                for packet in packets:
                    if packet.packet_type == 'welcome':
                        self._handle_welcome(packet)
                        break
                
                if not self.connected:
                    time.sleep(0.01)
            
            if self.connected:
                self.logger.info(f"Connected as player {self.player_id}")
                self._start_network_thread()
                self._start_game_thread()
                return True
            else:
                self.logger.error("Timed out waiting for welcome packet")
                sock.close()
                return False
                
        except Exception as e:
            self.logger.error(f"Connection error: {e}")
            return False

    def _handle_welcome(self, packet: NetworkPacket):
        self.player_id = packet.data.get('player_id', 0)
        self.connection.player_id = self.player_id
        
        world_data = packet.data.get('world', {})
        self.world = GameWorld.from_dict(world_data)
        self.predicted_world = GameWorld.from_dict(world_data)
        
        self.rollback.initialize(self.world)
        
        self.connected = True
        
        player = self.world.get_player(self.player_id)
        if player:
            self.logger.info(f"Welcome! You are {player.name} at position {player.position}")

    def _start_network_thread(self):
        self._thread = threading.Thread(target=self._network_loop, daemon=True)
        self._thread.start()

    def _start_game_thread(self):
        self._game_thread = threading.Thread(target=self._game_loop, daemon=True)
        self._game_thread.start()

    def _network_loop(self):
        self.logger.info("Network thread started")
        
        while not self._stop_event.is_set() and self.connected:
            if not self.connection or not self.connection.connected:
                self.connected = False
                break
            
            packets = self.connection.receive()
            for packet in packets:
                self._handle_packet(packet)
            
            self._send_ping()
            
            time.sleep(0.001)
        
        self.logger.info("Network thread stopped")

    def _game_loop(self):
        self.logger.info("Client game loop started")
        
        tick_interval = 1.0 / self.config.CLIENT_TICK_RATE
        last_tick = time.time()
        
        while not self._stop_event.is_set() and self.connected:
            now = time.time()
            delta = now - last_tick
            
            if delta >= tick_interval:
                last_tick = now
                self._tick()
            
            time.sleep(max(0, tick_interval - (time.time() - now)))
        
        self.logger.info("Client game loop stopped")

    def _tick(self):
        self._apply_pending_corrections()
        self._predict_local_player()
        self._send_input()
        self._update_interpolation()
        self._update_debug_info()

    def _handle_packet(self, packet: NetworkPacket):
        handlers = {
            'state': self._handle_state,
            'correction': self._handle_correction,
            'input_ack': self._handle_input_ack,
            'player_joined': self._handle_player_joined,
            'player_left': self._handle_player_left,
            'pong': self._handle_pong,
        }
        
        handler = handlers.get(packet.packet_type)
        if handler:
            handler(packet)
        else:
            self.logger.debug(f"Unhandled packet type: {packet.packet_type}")

    def _handle_state(self, packet: NetworkPacket):
        world_data = packet.data.get('world', {})
        server_world = GameWorld.from_dict(world_data)
        
        if server_world.frame_number <= self.last_server_state_frame:
            return
        
        self.last_server_state_frame = server_world.frame_number
        
        for player in server_world.players:
            if player.player_id == self.player_id:
                continue
            
            if player.is_connected:
                self.interpolated_positions[player.player_id] = player.position.clone()
        
        if self.world:
            for player in server_world.players:
                if player.player_id != self.player_id:
                    local_player = self.world.get_player(player.player_id)
                    if local_player:
                        local_player.position = player.position.clone()
                        local_player.authoritative_position = player.position.clone()
        
        self.rollback.confirm_frame(server_world.frame_number)

    def _handle_correction(self, packet: NetworkPacket):
        position_data = packet.data.get('position', (0, 0))
        corrected_position = Vector2(*position_data)
        corrected_frame = packet.data.get('frame', packet.frame_number)
        
        if corrected_frame <= self.last_server_state_frame:
            return
        
        self.pending_corrections.append((corrected_frame, corrected_position))
        self.debug_info['total_corrections'] += 1

    def _apply_pending_corrections(self):
        if not self.world or not self.predicted_world:
            return
        
        corrections_to_apply = []
        for frame, position in sorted(self.pending_corrections):
            if frame <= self.rollback.confirmed_frame:
                corrections_to_apply.append((frame, position))
        
        for frame, position in corrections_to_apply:
            self.pending_corrections.remove((frame, position))
            
            has_error, distance = self.rollback.check_prediction_error(
                self.player_id, position, frame
            )
            
            if has_error:
                self.debug_info['prediction_errors'] += 1
                self.logger.debug(
                    f"Prediction error at frame {frame}: distance={distance:.2f}"
                )
                
                if distance > 2.0:
                    was_rolled_back, frames_rolled = self.rollback.apply_correction(
                        self.player_id, position, frame
                    )
                    
                    if was_rolled_back:
                        self.debug_info['rollback_count'] += 1
                        
                        current_state = self.rollback.get_state_at_frame(
                            self.rollback.current_frame
                        )
                        if current_state:
                            self.world = current_state
                            self.predicted_world = GameWorld.from_dict(current_state.to_dict())
                else:
                    self.debug_info['smooth_corrections'] += 1
                    
                    player = self.world.get_player(self.player_id)
                    predicted_player = self.predicted_world.get_player(self.player_id)
                    
                    if player and predicted_player:
                        player.position = player.position.lerp(
                            position, self.config.CORRECTION_SMOOTHING
                        )
                        player.authoritative_position = position.clone()
                        
                        predicted_player.position = predicted_player.position.lerp(
                            position, self.config.CORRECTION_SMOOTHING
                        )

    def _predict_local_player(self):
        if not self.world or not self.predicted_world:
            return
        
        input_copy = InputState(
            left=self.current_input.left,
            right=self.current_input.right,
            up=self.current_input.up,
            down=self.current_input.down,
            action=self.current_input.action,
            sequence=self.input_sequence
        )
        
        self.rollback.add_input(self.player_id, input_copy)
        
        current_state = self.rollback.get_state_at_frame(self.rollback.current_frame)
        if current_state:
            self.predicted_world = GameWorld.from_dict(current_state.to_dict())
        
        player = self.predicted_world.get_player(self.player_id)
        if not player:
            return
        
        move_vec = self.current_input.to_vector()
        velocity = move_vec * self.config.PLAYER_SPEED
        new_position = player.position + velocity * self.config.FIXED_TIMESTEP
        
        radius = self.config.PLAYER_SIZE / 2
        wall_correction = self.predicted_world.check_wall_collision(new_position, radius)
        if wall_correction is not None:
            new_position = wall_correction
        
        player.position = new_position
        player.velocity = velocity
        player.predicted_position = new_position.clone()
        
        world_player = self.world.get_player(self.player_id)
        if world_player:
            world_player.predicted_position = new_position.clone()
            world_player.authoritative_position = player.authoritative_position.clone()

    def _send_input(self):
        if not self.connection or not self.connection.connected:
            return
        
        input_copy = InputState(
            left=self.current_input.left,
            right=self.current_input.right,
            up=self.current_input.up,
            down=self.current_input.down,
            action=self.current_input.action,
            sequence=self.input_sequence
        )
        
        frame_number = self.rollback.current_frame
        
        packet = self.create_input_packet(
            self.player_id, input_copy, frame_number
        )
        
        if self.connection.send(packet):
            self.input_sequence += 1

    def _handle_input_ack(self, packet: NetworkPacket):
        self.acknowledged_frames.add(packet.frame_number)
        
        while len(self.acknowledged_frames) > 100:
            min_frame = min(self.acknowledged_frames)
            self.acknowledged_frames.discard(min_frame)

    def _handle_player_joined(self, packet: NetworkPacket):
        player_data = packet.data.get('player', {})
        player = PlayerState.from_dict(player_data)
        
        self.logger.info(f"Player {player.name} joined the game")
        
        if self.world:
            self.world.add_player(player)
        if self.predicted_world:
            self.predicted_world.add_player(
                PlayerState.from_dict(player.to_dict())
            )

    def _handle_player_left(self, packet: NetworkPacket):
        player_id = packet.data.get('player_id', -1)
        
        self.logger.info(f"Player {player_id} left the game")
        
        if self.world:
            player = self.world.get_player(player_id)
            if player:
                player.is_connected = False
        
        if self.predicted_world:
            player = self.predicted_world.get_player(player_id)
            if player:
                player.is_connected = False

    def _handle_pong(self, packet: NetworkPacket):
        if self.connection:
            sent_time = self.connection.sent_packets.pop(packet.frame_number, None)
            if sent_time:
                ping = (time.time() - sent_time) * 1000
                self.average_ping = ping * 0.1 + self.average_ping * 0.9

    def _send_ping(self):
        if not self.connection or not self.connection.connected:
            return
        
        if int(time.time() * 1000) % 1000 < 16:
            ping_packet = NetworkPacket(
                packet_type='ping',
                player_id=self.player_id,
                frame_number=int(time.time() * 1000)
            )
            self.connection.send(ping_packet)

    def _update_interpolation(self):
        if not self.world:
            return
        
        for player_id, target_pos in self.interpolated_positions.items():
            if player_id == self.player_id:
                continue
            
            player = self.world.get_player(player_id)
            if player and player.is_connected:
                player.position = player.position.lerp(
                    target_pos, self.config.CORRECTION_SMOOTHING
                )

    def _update_debug_info(self):
        if int(time.time() * 2) % 2 == 0:
            return
        
        stats = self.rollback.get_statistics()
        self.logger.debug(
            f"Frame {self.rollback.current_frame}: "
            f"Ping={self.average_ping:.0f}ms, "
            f"Rollbacks={self.debug_info['rollback_count']}, "
            f"PredErrors={self.debug_info['prediction_errors']}, "
            f"SmoothCorr={self.debug_info['smooth_corrections']}"
        )

    def set_input(self, left: bool = None, right: bool = None, 
                  up: bool = None, down: bool = None, action: bool = None):
        if left is not None:
            self.current_input.left = left
        if right is not None:
            self.current_input.right = right
        if up is not None:
            self.current_input.up = up
        if down is not None:
            self.current_input.down = down
        if action is not None:
            self.current_input.action = action

    def get_local_player(self) -> Optional[PlayerState]:
        if self.predicted_world:
            return self.predicted_world.get_player(self.player_id)
        if self.world:
            return self.world.get_player(self.player_id)
        return None

    def get_world_state(self) -> Optional[GameWorld]:
        return self.predicted_world or self.world

    def get_debug_info(self) -> dict:
        stats = self.rollback.get_statistics()
        return {
            **self.debug_info,
            **stats,
            'ping_ms': self.average_ping,
            'predicted_frames_ahead': self.rollback.current_frame - self.rollback.confirmed_frame,
            'connected': self.connected,
        }

    def stop(self):
        self.logger.info("Stopping client...")
        self._stop_event.set()
        self.connected = False
        
        if self.connection:
            self.connection.close()
        
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        
        if self._game_thread and self._game_thread.is_alive():
            self._game_thread.join(timeout=2.0)
        
        self.logger.info("Client stopped")
