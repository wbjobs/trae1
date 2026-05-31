import dataclasses
from typing import Tuple


@dataclasses.dataclass
class GameConfig:
    FIXED_TIMESTEP: float = 1.0 / 60.0
    PREDICTION_WINDOW_MS: int = 100
    MAX_ROLLBACK_FRAMES: int = 10
    SERVER_TICK_RATE: int = 60
    CLIENT_TICK_RATE: int = 60
    INTERPOLATION_BUFFER_MS: int = 50
    CORRECTION_SMOOTHING: float = 0.1
    PLAYER_SPEED: float = 200.0
    PLAYER_SIZE: float = 30.0
    WORLD_WIDTH: float = 800.0
    WORLD_HEIGHT: float = 600.0
    MAX_PLAYERS: int = 4
    NETWORK_PORT: int = 5555
    DEBUG_VISUALIZATION: bool = True
    LOG_LEVEL: str = "INFO"

    @property
    def max_prediction_frames(self) -> int:
        return int(self.PREDICTION_WINDOW_MS / (self.FIXED_TIMESTEP * 1000)) + 1


DEFAULT_CONFIG = GameConfig()
