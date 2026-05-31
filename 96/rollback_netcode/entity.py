import dataclasses
import math
from typing import List, Tuple, Optional
from dataclasses import dataclass


@dataclass
class Vector2:
    x: float = 0.0
    y: float = 0.0

    def __add__(self, other: 'Vector2') -> 'Vector2':
        return Vector2(self.x + other.x, self.y + other.y)

    def __sub__(self, other: 'Vector2') -> 'Vector2':
        return Vector2(self.x - other.x, self.y - other.y)

    def __mul__(self, scalar: float) -> 'Vector2':
        return Vector2(self.x * scalar, self.y * scalar)

    def __truediv__(self, scalar: float) -> 'Vector2':
        return Vector2(self.x / scalar, self.y / scalar)

    def length(self) -> float:
        return math.sqrt(self.x * self.x + self.y * self.y)

    def normalized(self) -> 'Vector2':
        length = self.length()
        if length == 0:
            return Vector2(0, 0)
        return Vector2(self.x / length, self.y / length)

    def clamp(self, min_x: float, max_x: float, min_y: float, max_y: float) -> 'Vector2':
        return Vector2(
            max(min_x, min(max_x, self.x)),
            max(min_y, min(max_y, self.y))
        )

    def to_tuple(self) -> Tuple[float, float]:
        return (self.x, self.y)

    def distance_to(self, other: 'Vector2') -> float:
        return (self - other).length()

    def lerp(self, target: 'Vector2', t: float) -> 'Vector2':
        return Vector2(
            self.x + (target.x - self.x) * t,
            self.y + (target.y - self.y) * t
        )

    def clone(self) -> 'Vector2':
        return Vector2(self.x, self.y)


@dataclass
class InputState:
    left: bool = False
    right: bool = False
    up: bool = False
    down: bool = False
    action: bool = False
    sequence: int = 0

    def to_vector(self) -> Vector2:
        vec = Vector2(0, 0)
        if self.left:
            vec.x -= 1
        if self.right:
            vec.x += 1
        if self.up:
            vec.y -= 1
        if self.down:
            vec.y += 1
        if vec.length() > 0:
            return vec.normalized()
        return vec

    def to_dict(self) -> dict:
        return {
            'left': self.left,
            'right': self.right,
            'up': self.up,
            'down': self.down,
            'action': self.action,
            'sequence': self.sequence
        }

    @classmethod
    def from_dict(cls, data: dict) -> 'InputState':
        return cls(
            left=data.get('left', False),
            right=data.get('right', False),
            up=data.get('up', False),
            down=data.get('down', False),
            action=data.get('action', False),
            sequence=data.get('sequence', 0)
        )


@dataclass
class PlayerState:
    player_id: int
    position: Vector2 = dataclasses.field(default_factory=Vector2)
    velocity: Vector2 = dataclasses.field(default_factory=Vector2)
    predicted_position: Vector2 = dataclasses.field(default_factory=Vector2)
    authoritative_position: Vector2 = dataclasses.field(default_factory=Vector2)
    size: float = 30.0
    color: Tuple[int, int, int] = (255, 100, 100)
    name: str = "Player"
    is_connected: bool = False
    last_input_sequence: int = 0
    confirmed_frame: int = 0

    def to_dict(self) -> dict:
        return {
            'player_id': self.player_id,
            'position': self.position.to_tuple(),
            'velocity': self.velocity.to_tuple(),
            'predicted_position': self.predicted_position.to_tuple(),
            'authoritative_position': self.authoritative_position.to_tuple(),
            'size': self.size,
            'color': list(self.color),
            'name': self.name,
            'is_connected': self.is_connected,
            'last_input_sequence': self.last_input_sequence,
            'confirmed_frame': self.confirmed_frame
        }

    @classmethod
    def from_dict(cls, data: dict) -> 'PlayerState':
        return cls(
            player_id=data['player_id'],
            position=Vector2(*data.get('position', (0, 0))),
            velocity=Vector2(*data.get('velocity', (0, 0))),
            predicted_position=Vector2(*data.get('predicted_position', (0, 0))),
            authoritative_position=Vector2(*data.get('authoritative_position', (0, 0))),
            size=data.get('size', 30.0),
            color=tuple(data.get('color', (255, 100, 100))),
            name=data.get('name', 'Player'),
            is_connected=data.get('is_connected', False),
            last_input_sequence=data.get('last_input_sequence', 0),
            confirmed_frame=data.get('confirmed_frame', 0)
        )


@dataclass
class CollisionBox:
    position: Vector2 = dataclasses.field(default_factory=Vector2)
    width: float = 50.0
    height: float = 50.0
    is_solid: bool = True
    color: Tuple[int, int, int] = (100, 100, 100)

    def contains(self, point: Vector2, player_radius: float = 0) -> bool:
        return (
            point.x + player_radius >= self.position.x and
            point.x - player_radius <= self.position.x + self.width and
            point.y + player_radius >= self.position.y and
            point.y - player_radius <= self.position.y + self.height
        )

    def resolve_collision(self, player_pos: Vector2, player_radius: float) -> Optional[Vector2]:
        if not self.contains(player_pos, player_radius):
            return None

        left = abs((player_pos.x + player_radius) - self.position.x)
        right = abs((player_pos.x - player_radius) - (self.position.x + self.width))
        top = abs((player_pos.y + player_radius) - self.position.y)
        bottom = abs((player_pos.y - player_radius) - (self.position.y + self.height))

        min_dist = min(left, right, top, bottom)

        if min_dist == left:
            return Vector2(self.position.x - player_radius, player_pos.y)
        elif min_dist == right:
            return Vector2(self.position.x + self.width + player_radius, player_pos.y)
        elif min_dist == top:
            return Vector2(player_pos.x, self.position.y - player_radius)
        else:
            return Vector2(player_pos.x, self.position.y + self.height + player_radius)

    def to_dict(self) -> dict:
        return {
            'position': self.position.to_tuple(),
            'width': self.width,
            'height': self.height,
            'is_solid': self.is_solid,
            'color': list(self.color)
        }


@dataclass
class GameWorld:
    width: float = 800.0
    height: float = 600.0
    walls: List[CollisionBox] = dataclasses.field(default_factory=list)
    players: List[PlayerState] = dataclasses.field(default_factory=list)
    frame_number: int = 0
    checksum: int = 0

    def add_wall(self, x: float, y: float, width: float, height: float, 
                 color: Tuple[int, int, int] = (100, 100, 100)):
        wall = CollisionBox(
            position=Vector2(x, y),
            width=width,
            height=height,
            color=color
        )
        self.walls.append(wall)
        return wall

    def add_player(self, player: PlayerState):
        while len(self.players) <= player.player_id:
            self.players.append(PlayerState(player_id=len(self.players)))
        self.players[player.player_id] = player
        player.is_connected = True

    def get_player(self, player_id: int) -> Optional[PlayerState]:
        if 0 <= player_id < len(self.players):
            return self.players[player_id]
        return None

    def check_wall_collision(self, position: Vector2, radius: float) -> Optional[Vector2]:
        corrected_pos = position
        collided = False

        for wall in self.walls:
            resolved = wall.resolve_collision(corrected_pos, radius)
            if resolved is not None:
                corrected_pos = resolved
                collided = True

        corrected_pos = corrected_pos.clamp(
            radius, self.width - radius,
            radius, self.height - radius
        )

        return corrected_pos if collided else None

    def check_player_collision(self, player_id: int, position: Vector2, 
                               radius: float) -> Optional[Vector2]:
        corrected_pos = position
        collided = False

        for other in self.players:
            if other.player_id == player_id or not other.is_connected:
                continue
            dist = position.distance_to(other.position)
            min_dist = radius + other.size / 2
            if dist < min_dist and dist > 0:
                push_dir = (position - other.position).normalized()
                overlap = min_dist - dist
                corrected_pos = corrected_pos + push_dir * overlap
                collided = True

        return corrected_pos if collided else None

    def to_dict(self) -> dict:
        return {
            'width': self.width,
            'height': self.height,
            'walls': [w.to_dict() for w in self.walls],
            'players': [p.to_dict() for p in self.players],
            'frame_number': self.frame_number,
            'checksum': self.checksum
        }

    @classmethod
    def from_dict(cls, data: dict) -> 'GameWorld':
        world = cls(
            width=data.get('width', 800.0),
            height=data.get('height', 600.0),
            frame_number=data.get('frame_number', 0),
            checksum=data.get('checksum', 0)
        )
        
        for wall_data in data.get('walls', []):
            wall = CollisionBox(
                position=Vector2(*wall_data['position']),
                width=wall_data['width'],
                height=wall_data['height'],
                is_solid=wall_data.get('is_solid', True),
                color=tuple(wall_data.get('color', (100, 100, 100)))
            )
            world.walls.append(wall)
        
        for player_data in data.get('players', []):
            player = PlayerState.from_dict(player_data)
            world.players.append(player)
        
        return world
