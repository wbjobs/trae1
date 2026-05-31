import logging
import socket
import threading
import time
from typing import Dict, List, Optional

from entity import Vector2, InputState, PlayerState, GameWorld
from physics import DeterministicPhysics
from rollback import RollbackManager
from network import BaseNetwork, Connection, NetworkPacket
from config import GameConfig, DEFAULT_CONFIG


class GameServer(BaseNetwork):
    def __init__(self, config: GameConfig = DEFAULT_CONFIG, host: str = '0.0.0.0'):
        super().__init__(config)
        self.host = host
        self.port = config.NETWORK_PORT
        
        self.logger = logging.getLogger("GameServer")
        self.logger.setLevel(getattr(logging, config.LOG_LEVEL))
        
        self.world = GameWorld(width=config.WORLD_WIDTH, height=config.WORLD_HEIGHT)
        self.physics = DeterministicPhysics(config)
        self.rollback = RollbackManager(config, self.physics)
        
        self.connections: Dict[int, Connection] = {}
        self.pending_inputs: Dict[int, Dict[int, InputState]] = {}
        self.next_player_id = 0
        
        self.server_sock: Optional[socket.socket] = None
        self._tick_thread: Optional[threading.Thread] = None
        
        self._setup_world()
        self.rollback.initialize(self.world)

    def _setup_world(self):
        self.world.add_wall(200, 150, 400, 20, (80, 80, 80))
        self.world.add_wall(200, 430, 400, 20, (80, 80, 80))
        self.world.add_wall(150, 200, 20, 200, (80, 80, 80))
        self.world.add_wall(630, 200, 20, 200, (80, 80, 80))
        self.world.add_wall(350, 280, 100, 40, (120, 80, 80))
        
        self.logger.info(f"World initialized: {len(self.world.walls)} walls")

    def start(self):
        self.logger.info(f"Starting server on {self.host}:{self.port}")
        
        self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_sock.bind((self.host, self.port))
        self.server_sock.listen(self.config.MAX_PLAYERS)
        self.server_sock.settimeout(0.1)
        
        self._thread = threading.Thread(target=self._accept_thread, daemon=True)
        self._thread.start()
        
        self._tick_thread = threading.Thread(target=self._game_loop, daemon=True)
        self._tick_thread.start()
        
        self.logger.info("Server started successfully")

    def _accept_thread(self):
        self.logger.info("Accept thread started")
        
        while not self._stop_event.is_set():
            try:
                client_sock, addr = self.server_sock.accept()
                client_sock.settimeout(0.1)
                
                if len(self.connections) >= self.config.MAX_PLAYERS:
                    self.logger.warning(f"Max players reached, rejecting {addr}")
                    client_sock.close()
                    continue
                
                player_id = self.next_player_id
                self.next_player_id += 1
                
                connection = Connection(client_sock, addr, player_id)
                self.connections[player_id] = connection
                
                colors = [(255, 100, 100), (100, 255, 100), (100, 100, 255), (255, 255, 100)]
                start_positions = [
                    Vector2(100, 300),
                    Vector2(700, 300),
                    Vector2(400, 100),
                    Vector2(400, 500)
                ]
                
                player = PlayerState(
                    player_id=player_id,
                    position=start_positions[player_id % len(start_positions)],
                    size=self.config.PLAYER_SIZE,
                    color=colors[player_id % len(colors)],
                    name=f"Player{player_id + 1}",
                    is_connected=True
                )
                
                self.world.add_player(player)
                
                self.logger.info(f"Player {player_id} connected from {addr}")
                self.logger.info(f"Active players: {len(self.connections)}")
                
                welcome_packet = NetworkPacket(
                    packet_type='welcome',
                    player_id=player_id,
                    data={
                        'player_id': player_id,
                        'world': self.world.to_dict(),
                        'config': {
                            'FIXED_TIMESTEP': self.config.FIXED_TIMESTEP,
                            'PREDICTION_WINDOW_MS': self.config.PREDICTION_WINDOW_MS,
                            'PLAYER_SPEED': self.config.PLAYER_SPEED,
                            'PLAYER_SIZE': self.config.PLAYER_SIZE,
                            'WORLD_WIDTH': self.config.WORLD_WIDTH,
                            'WORLD_HEIGHT': self.config.WORLD_HEIGHT,
                        }
                    }
                )
                connection.send(welcome_packet)
                
                self._broadcast_player_joined(player)
                
            except socket.timeout:
                continue
            except Exception as e:
                self.logger.error(f"Accept error: {e}")

    def _game_loop(self):
        self.logger.info("Game loop started")
        
        tick_interval = 1.0 / self.config.SERVER_TICK_RATE
        last_tick = time.time()
        
        frame = 0
        
        while not self._stop_event.is_set():
            now = time.time()
            delta = now - last_tick
            
            if delta >= tick_interval:
                last_tick = now
                
                self._process_network_messages()
                self._tick()
                self._broadcast_state()
                self._check_disconnects()
                
                frame += 1
                
                if frame % 60 == 0:
                    self._log_stats()
            
            else:
                time.sleep(max(0, tick_interval - delta))

    def _process_network_messages(self):
        for player_id in list(self.connections.keys()):
            conn = self.connections[player_id]
            if not conn.connected:
                continue
            
            packets = conn.receive()
            for packet in packets:
                self._handle_packet(player_id, packet)

    def _handle_packet(self, player_id: int, packet: NetworkPacket):
        if packet.packet_type == 'input':
            input_data = packet.data.get('input', {})
            input_state = InputState.from_dict(input_data)
            input_state.sequence = packet.frame_number
            
            if player_id not in self.pending_inputs:
                self.pending_inputs[player_id] = {}
            
            self.pending_inputs[player_id][packet.frame_number] = input_state
            
            self.rollback.add_input(player_id, input_state)
            
            ack_packet = NetworkPacket(
                packet_type='input_ack',
                player_id=player_id,
                frame_number=packet.frame_number
            )
            conn = self.connections.get(player_id)
            if conn:
                conn.send(ack_packet)
                
        elif packet.packet_type == 'ping':
            pong_packet = NetworkPacket(
                packet_type='pong',
                player_id=player_id,
                frame_number=packet.frame_number
            )
            conn = self.connections.get(player_id)
            if conn:
                conn.send(pong_packet)

    def _tick(self):
        current_frame = self.world.frame_number
        
        frame_inputs = {}
        for player in self.world.players:
            if not player.is_connected:
                continue
            
            player_input = InputState()
            if player.player_id in self.pending_inputs:
                player_input = self.pending_inputs[player.player_id].get(current_frame, InputState())
            
            frame_inputs[player.player_id] = player_input
        
        self.physics.step(self.world, frame_inputs)
        self.rollback.save_state(self.world)
        
        self._send_corrections()
        
        for player_id in list(self.pending_inputs.keys()):
            if current_frame in self.pending_inputs[player_id]:
                del self.pending_inputs[player_id][current_frame]

    def _send_corrections(self):
        for player_id, conn in self.connections.items():
            if not conn.connected:
                continue
            
            player = self.world.get_player(player_id)
            if not player:
                continue
            
            correction_packet = self.create_correction_packet(
                player_id,
                self.world.frame_number,
                player.position
            )
            conn.send(correction_packet)

    def _broadcast_state(self):
        state_packet = self.create_state_packet(self.world)
        
        for player_id, conn in list(self.connections.items()):
            if conn.connected:
                conn.send(state_packet)

    def _broadcast_player_joined(self, player: PlayerState):
        packet = NetworkPacket(
            packet_type='player_joined',
            player_id=player.player_id,
            data={'player': player.to_dict()}
        )
        
        for pid, conn in self.connections.items():
            if conn.connected:
                conn.send(packet)

    def _broadcast_player_left(self, player_id: int):
        packet = NetworkPacket(
            packet_type='player_left',
            player_id=player_id,
            data={'player_id': player_id}
        )
        
        for pid, conn in self.connections.items():
            if conn.connected:
                conn.send(packet)

    def _check_disconnects(self):
        timeout = 5.0
        now = time.time()
        
        for player_id in list(self.connections.keys()):
            conn = self.connections[player_id]
            if not conn.connected or (now - conn.last_packet_time) > timeout:
                self.logger.info(f"Player {player_id} disconnected")
                self._remove_player(player_id)

    def _remove_player(self, player_id: int):
        conn = self.connections.pop(player_id, None)
        if conn:
            conn.close()
        
        player = self.world.get_player(player_id)
        if player:
            player.is_connected = False
        
        if player_id in self.pending_inputs:
            del self.pending_inputs[player_id]
        
        self._broadcast_player_left(player_id)

    def _log_stats(self):
        stats = self.rollback.get_statistics()
        self.logger.info(
            f"Frame {self.world.frame_number}: "
            f"Players={len(self.connections)}, "
            f"Rollbacks={stats['rollback_count']}, "
            f"AvgRollbackFrames={stats['avg_rollback_frames']:.1f}"
        )

    def stop(self):
        self.logger.info("Stopping server...")
        super().stop()
        
        for player_id in list(self.connections.keys()):
            self._remove_player(player_id)
        
        if self.server_sock:
            try:
                self.server_sock.close()
            except Exception:
                pass
        
        self.logger.info("Server stopped")

    def get_world(self) -> GameWorld:
        return self.world
