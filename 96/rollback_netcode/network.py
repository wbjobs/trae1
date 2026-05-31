import json
import socket
import threading
import time
from typing import Dict, List, Optional, Tuple, Any, Callable
from dataclasses import dataclass, field

from entity import Vector2, InputState, PlayerState, GameWorld
from config import GameConfig


@dataclass
class NetworkPacket:
    packet_type: str
    player_id: int = 0
    frame_number: int = 0
    timestamp: float = field(default_factory=time.time)
    data: dict = field(default_factory=dict)

    def to_bytes(self) -> bytes:
        packet_dict = {
            'type': self.packet_type,
            'player_id': self.player_id,
            'frame': self.frame_number,
            'ts': self.timestamp,
            'data': self.data
        }
        return (json.dumps(packet_dict) + '\n').encode('utf-8')

    @classmethod
    def from_bytes(cls, raw_data: bytes) -> Optional['NetworkPacket']:
        try:
            packet_dict = json.loads(raw_data.decode('utf-8'))
            return cls(
                packet_type=packet_dict['type'],
                player_id=packet_dict.get('player_id', 0),
                frame_number=packet_dict.get('frame', 0),
                timestamp=packet_dict.get('ts', time.time()),
                data=packet_dict.get('data', {})
            )
        except (json.JSONDecodeError, KeyError):
            return None


class Connection:
    def __init__(self, sock: socket.socket, addr: Tuple[str, int], player_id: int):
        self.sock = sock
        self.addr = addr
        self.player_id = player_id
        self.connected = True
        self.last_packet_time = time.time()
        self.buffer = b''
        self.ping = 0.0
        self.sent_packets: Dict[int, float] = {}
        self.acknowledged_frames = set()

    def send(self, packet: NetworkPacket) -> bool:
        try:
            self.sock.sendall(packet.to_bytes())
            if packet.packet_type == 'ping':
                self.sent_packets[packet.frame_number] = time.time()
            return True
        except Exception:
            self.connected = False
            return False

    def receive(self) -> List[NetworkPacket]:
        if not self.connected:
            return []
        
        packets = []
        try:
            data = self.sock.recv(4096)
            if not data:
                self.connected = False
                return []
            
            self.buffer += data
            
            while b'\n' in self.buffer:
                line, self.buffer = self.buffer.split(b'\n', 1)
                if line:
                    packet = NetworkPacket.from_bytes(line)
                    if packet:
                        self.last_packet_time = time.time()
                        
                        if packet.packet_type == 'pong':
                            sent_time = self.sent_packets.pop(packet.frame_number, None)
                            if sent_time:
                                self.ping = (time.time() - sent_time) * 1000
                        
                        packets.append(packet)
            
            return packets
        except socket.timeout:
            return []
        except Exception:
            self.connected = False
            return []

    def close(self):
        self.connected = False
        try:
            self.sock.close()
        except Exception:
            pass


class BaseNetwork:
    def __init__(self, config: GameConfig):
        self.config = config
        self.logger = None
        self.callbacks: Dict[str, Callable] = {}
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def on(self, event_type: str, callback: Callable):
        self.callbacks[event_type] = callback

    def _emit(self, event_type: str, *args, **kwargs):
        callback = self.callbacks.get(event_type)
        if callback:
            try:
                callback(*args, **kwargs)
            except Exception as e:
                if self.logger:
                    self.logger.error(f"Callback error for {event_type}: {e}")

    def start(self):
        raise NotImplementedError

    def stop(self):
        self._stop_event.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)

    def create_input_packet(self, player_id: int, input_state: InputState, 
                            frame_number: int) -> NetworkPacket:
        return NetworkPacket(
            packet_type='input',
            player_id=player_id,
            frame_number=frame_number,
            data={'input': input_state.to_dict()}
        )

    def create_state_packet(self, world: GameWorld) -> NetworkPacket:
        return NetworkPacket(
            packet_type='state',
            frame_number=world.frame_number,
            data={'world': world.to_dict()}
        )

    def create_correction_packet(self, player_id: int, frame_number: int,
                                  position: Vector2) -> NetworkPacket:
        return NetworkPacket(
            packet_type='correction',
            player_id=player_id,
            frame_number=frame_number,
            data={
                'position': position.to_tuple(),
                'frame': frame_number
            }
        )
