import logging
import threading
import time
from typing import Optional, Dict, List

from entity import Vector2, PlayerState, GameWorld
from client import GameClient
from config import GameConfig


class DebugVisualizer:
    def __init__(self, client: GameClient, config: GameConfig, headless: bool = False):
        self.client = client
        self.config = config
        self.headless = headless
        self.logger = logging.getLogger("Visualizer")
        
        self.pygame_available = False
        self.screen = None
        self.clock = None
        self.font = None
        
        self._thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        
        self.rollback_animation_time = 0.0
        self.last_rollback_count = 0
        
        self.position_history: List[Dict[int, Vector2]] = []
        self.max_history = 60
        
        self._init_pygame()

    def _init_pygame(self):
        if self.headless:
            return
        
        try:
            import pygame
            pygame.init()
            
            self.screen = pygame.display.set_mode(
                (int(self.config.WORLD_WIDTH), int(self.config.WORLD_HEIGHT) + 150)
            )
            pygame.display.set_caption("Rollback Netcode - Debug Visualizer")
            
            self.clock = pygame.time.Clock()
            self.font = pygame.font.SysFont('Consolas', 14)
            self.small_font = pygame.font.SysFont('Consolas', 12)
            
            self.pygame_available = True
            self.logger.info("Pygame initialized successfully")
            
        except ImportError:
            self.logger.warning("Pygame not available, running in headless mode")
            self.headless = True
        except Exception as e:
            self.logger.warning(f"Failed to initialize pygame: {e}")
            self.headless = True

    def start(self):
        if not self.pygame_available:
            self.logger.info("Visualizer running in headless mode (text output only)")
        
        self._thread = threading.Thread(target=self._render_loop, daemon=True)
        self._thread.start()

    def _render_loop(self):
        while not self._stop_event.is_set():
            if self.pygame_available:
                self._render()
            else:
                self._text_output()
            
            self._update_history()
            
            time.sleep(0.016)

    def _render(self):
        import pygame
        
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                self._stop_event.set()
                return
            elif event.type == pygame.KEYDOWN:
                self._handle_key(event.key, True)
            elif event.type == pygame.KEYUP:
                self._handle_key(event.key, False)
        
        self.screen.fill((20, 20, 30))
        
        world = self.client.get_world_state()
        if not world:
            self._render_loading()
            pygame.display.flip()
            self.clock.tick(60)
            return
        
        self._render_world(world)
        self._render_players(world)
        self._render_trajectory(world)
        self._render_debug_panel()
        self._render_rollback_indicator()
        
        pygame.display.flip()
        self.clock.tick(60)

    def _render_world(self, world: GameWorld):
        import pygame
        
        pygame.draw.rect(
            self.screen, (30, 30, 40),
            (0, 0, world.width, world.height)
        )
        
        grid_color = (40, 40, 50)
        for x in range(0, int(world.width), 50):
            pygame.draw.line(self.screen, grid_color, (x, 0), (x, world.height))
        for y in range(0, int(world.height), 50):
            pygame.draw.line(self.screen, grid_color, (0, y), (world.width, y))
        
        for wall in world.walls:
            pygame.draw.rect(
                self.screen, wall.color,
                (int(wall.position.x), int(wall.position.y),
                 int(wall.width), int(wall.height))
            )
            
            pygame.draw.rect(
                self.screen, (200, 200, 200),
                (int(wall.position.x), int(wall.position.y),
                 int(wall.width), int(wall.height)), 2
            )
        
        pygame.draw.rect(
            self.screen, (100, 100, 120),
            (0, 0, world.width, world.height), 3
        )

    def _render_players(self, world: GameWorld):
        import pygame
        
        for player in world.players:
            if not player.is_connected:
                continue
            
            if player.player_id == self.client.player_id:
                auth_pos = player.authoritative_position
                pred_pos = player.predicted_position
                
                self._draw_player(
                    auth_pos, player.size, (255, 255, 255),
                    filled=False, line_width=2, alpha=128
                )
                
                self._draw_player(
                    pred_pos, player.size, player.color,
                    filled=True, line_width=0
                )
                
                if pred_pos.distance_to(auth_pos) > 0.5:
                    pygame.draw.line(
                        self.screen, (255, 255, 0),
                        (int(auth_pos.x), int(auth_pos.y)),
                        (int(pred_pos.x), int(pred_pos.y)), 2
                    )
                
                error_dist = pred_pos.distance_to(auth_pos)
                if error_dist > 1.0:
                    text = self.small_font.render(
                        f"Δ{error_dist:.1f}", True, (255, 255, 0))
                    self.screen.blit(
                        text, (int(pred_pos.x) + 20, int(pred_pos.y) - 20))
            else:
                self._draw_player(
                    player.position, player.size, player.color,
                    filled=True, line_width=0
                )
            
            name_text = self.font.render(player.name, True, (255, 255, 255))
            self.screen.blit(
                name_text,
                (int(player.position.x) - 25, int(player.position.y) - player.size - 15)
            )

    def _draw_player(self, position: Vector2, size: float, color: tuple,
                     filled: bool = True, line_width: int = 2, alpha: int = 255):
        import pygame
        
        if alpha < 255:
            surface = pygame.Surface((int(size * 2), int(size * 2)), pygame.SRCALPHA)
            draw_color = (*color, alpha)
            pygame.draw.circle(
                surface, draw_color, (int(size), int(size)), int(size / 2),
                0 if filled else line_width
            )
            self.screen.blit(
                surface,
                (int(position.x - size), int(position.y - size))
            )
        else:
            pygame.draw.circle(
                self.screen, color,
                (int(position.x), int(position.y)), int(size / 2),
                0 if filled else line_width
            )

    def _render_trajectory(self, world: GameWorld):
        import pygame
        
        if len(self.position_history) < 2:
            return
        
        player_id = self.client.player_id
        
        for i in range(len(self.position_history) - 1):
            frame_data = self.position_history[i]
            next_frame_data = self.position_history[i + 1]
            
            if player_id not in frame_data or player_id not in next_frame_data:
                continue
            
            pos1 = frame_data[player_id]
            pos2 = next_frame_data[player_id]
            
            alpha = int(255 * (i / len(self.position_history)))
            color = (100, 200, 255, alpha)
            
            surface = pygame.Surface(
                (int(self.config.WORLD_WIDTH), int(self.config.WORLD_HEIGHT)),
                pygame.SRCALPHA
            )
            pygame.draw.line(
                surface, color,
                (int(pos1.x), int(pos1.y)),
                (int(pos2.x), int(pos2.y)), 2
            )
            self.screen.blit(surface, (0, 0))

    def _render_debug_panel(self):
        import pygame
        
        panel_y = int(self.config.WORLD_HEIGHT) + 10
        panel_bg = pygame.Rect(0, panel_y - 5, self.config.WORLD_WIDTH, 145)
        pygame.draw.rect(self.screen, (10, 10, 20), panel_bg)
        
        debug_info = self.client.get_debug_info()
        
        lines = [
            f"PING: {debug_info.get('ping_ms', 0):.0f}ms | "
            f"FRAME: {debug_info.get('current_frame', 0)} | "
            f"PREDICTED_AHEAD: {debug_info.get('predicted_frames_ahead', 0)} frames",
            
            f"ROLLBACKS: {debug_info.get('rollback_count', 0)} | "
            f"AVG_ROLLBACK: {debug_info.get('avg_rollback_frames', 0):.1f} frames | "
            f"LAST_ROLLBACK: Frame {debug_info.get('last_rollback_frame', -1)}",
            
            f"CORRECTIONS: {debug_info.get('total_corrections', 0)} | "
            f"SMOOTH_CORR: {debug_info.get('smooth_corrections', 0)} | "
            f"PRED_ERRORS: {debug_info.get('prediction_errors', 0)}",
            
            f"STATE_HISTORY: {debug_info.get('history_size', 0)} | "
            f"CONFIRMED_FRAME: {debug_info.get('confirmed_frame', 0)} | "
            f"CONNECTED: {'YES' if debug_info.get('connected', False) else 'NO'}"
        ]
        
        for i, line in enumerate(lines):
            text = self.font.render(line, True, (200, 200, 200))
            self.screen.blit(text, (10, panel_y + i * 25))
        
        legend_x = int(self.config.WORLD_WIDTH) - 300
        legend_items = [
            ("Filled Circle", "Predicted Position", (255, 100, 100)),
            ("Outline Circle", "Authoritative Position", (255, 255, 255)),
            ("Yellow Line", "Prediction Error", (255, 255, 0)),
            ("Blue Trail", "Movement History", (100, 200, 255)),
        ]
        
        for i, (label, desc, color) in enumerate(legend_items):
            pygame.draw.circle(
                self.screen, color,
                (legend_x + 10, panel_y + i * 25 + 8), 5
            )
            text = self.small_font.render(f"{label}: {desc}", True, (150, 150, 150))
            self.screen.blit(text, (legend_x + 25, panel_y + i * 25))

    def _render_rollback_indicator(self):
        import pygame
        
        debug_info = self.client.get_debug_info()
        current_rollbacks = debug_info.get('rollback_count', 0)
        
        if current_rollbacks > self.last_rollback_count:
            self.rollback_animation_time = 1.0
            self.last_rollback_count = current_rollbacks
        
        if self.rollback_animation_time > 0:
            alpha = int(255 * self.rollback_animation_time)
            flash_surface = pygame.Surface(
                (int(self.config.WORLD_WIDTH), int(self.config.WORLD_HEIGHT)),
                pygame.SRCALPHA
            )
            flash_surface.fill((255, 100, 100, alpha // 3))
            self.screen.blit(flash_surface, (0, 0))
            
            self.rollback_animation_time -= 0.03
            
            text = self.font.render("ROLLBACK", True, (255, 100, 100))
            text_rect = text.get_rect(center=(self.config.WORLD_WIDTH // 2, 50))
            self.screen.blit(text, text_rect)

    def _render_loading(self):
        import pygame
        
        text = self.font.render("Connecting to server...", True, (255, 255, 255))
        text_rect = text.get_rect(
            center=(self.config.WORLD_WIDTH // 2, self.config.WORLD_HEIGHT // 2))
        self.screen.blit(text, text_rect)

    def _handle_key(self, key: int, pressed: bool):
        import pygame
        
        key_map = {
            pygame.K_LEFT: 'left',
            pygame.K_RIGHT: 'right',
            pygame.K_UP: 'up',
            pygame.K_DOWN: 'down',
            pygame.K_a: 'left',
            pygame.K_d: 'right',
            pygame.K_w: 'up',
            pygame.K_s: 'down',
            pygame.K_SPACE: 'action',
        }
        
        if key in key_map:
            input_name = key_map[key]
            self.client.set_input(**{input_name: pressed})

    def _update_history(self):
        world = self.client.get_world_state()
        if not world:
            return
        
        positions = {}
        for player in world.players:
            if player.is_connected:
                positions[player.player_id] = player.position.clone()
        
        self.position_history.append(positions)
        if len(self.position_history) > self.max_history:
            self.position_history.pop(0)

    def _text_output(self):
        if int(time.time() * 2) % 2 != 0:
            return
        
        debug_info = self.client.get_debug_info()
        world = self.client.get_world_state()
        
        output = "\n" + "=" * 60 + "\n"
        output += "ROLLBACK NETCODE DEBUG - HEADLESS MODE\n"
        output += "=" * 60 + "\n"
        
        if world:
            local_player = world.get_player(self.client.player_id)
            if local_player:
                output += f"Player Position: {local_player.position}\n"
                output += f"Predicted:      {local_player.predicted_position}\n"
                output += f"Authoritative:  {local_player.authoritative_position}\n"
                error = local_player.predicted_position.distance_to(
                    local_player.authoritative_position)
                output += f"Prediction Error: {error:.2f}\n"
        
        output += "\nNetwork Stats:\n"
        output += f"  Ping: {debug_info.get('ping_ms', 0):.0f}ms\n"
        output += f"  Current Frame: {debug_info.get('current_frame', 0)}\n"
        output += f"  Confirmed Frame: {debug_info.get('confirmed_frame', 0)}\n"
        output += f"  Predicted Ahead: {debug_info.get('predicted_frames_ahead', 0)} frames\n"
        
        output += "\nRollback Stats:\n"
        output += f"  Total Rollbacks: {debug_info.get('rollback_count', 0)}\n"
        output += f"  Total Rollback Frames: {debug_info.get('total_rollback_frames', 0)}\n"
        output += f"  Average Rollback: {debug_info.get('avg_rollback_frames', 0):.1f} frames\n"
        
        output += "\nCorrection Stats:\n"
        output += f"  Total Corrections: {debug_info.get('total_corrections', 0)}\n"
        output += f"  Smooth Corrections: {debug_info.get('smooth_corrections', 0)}\n"
        output += f"  Prediction Errors: {debug_info.get('prediction_errors', 0)}\n"
        
        output += "\nControls:\n"
        output += "  WASD or Arrow Keys: Move\n"
        output += "  SPACE: Action\n"
        output += "=" * 60 + "\n"
        
        print(output)

    def stop(self):
        self.logger.info("Stopping visualizer...")
        self._stop_event.set()
        
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1.0)
        
        if self.pygame_available:
            try:
                import pygame
                pygame.quit()
            except Exception:
                pass
        
        self.logger.info("Visualizer stopped")
