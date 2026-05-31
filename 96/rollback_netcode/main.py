#!/usr/bin/env python3
import argparse
import logging
import sys
import time

from config import GameConfig, DEFAULT_CONFIG
from server import GameServer
from client import GameClient
from visualizer import DebugVisualizer


def setup_logging(level: str = "INFO"):
    logging.basicConfig(
        level=getattr(logging, level),
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        datefmt='%H:%M:%S'
    )


def cmd_server(args):
    setup_logging(args.log_level)
    logger = logging.getLogger("Main")
    
    config = GameConfig(
        FIXED_TIMESTEP=args.timestep,
        PREDICTION_WINDOW_MS=args.prediction_window,
        MAX_ROLLBACK_FRAMES=args.max_rollback,
        SERVER_TICK_RATE=args.tick_rate,
        PLAYER_SPEED=args.player_speed,
        PLAYER_SIZE=args.player_size,
        WORLD_WIDTH=args.world_width,
        WORLD_HEIGHT=args.world_height,
        NETWORK_PORT=args.port,
        LOG_LEVEL=args.log_level
    )
    
    logger.info(f"Starting server on port {args.port}")
    logger.info(f"  Tick rate: {args.tick_rate} Hz")
    logger.info(f"  Prediction window: {args.prediction_window} ms")
    logger.info(f"  Max rollback frames: {args.max_rollback}")
    logger.info(f"  World size: {args.world_width}x{args.world_height}")
    
    server = GameServer(config, host=args.host)
    server.start()
    
    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        logger.info("Received shutdown signal")
    finally:
        server.stop()
        logger.info("Server shutdown complete")


def cmd_client(args):
    setup_logging(args.log_level)
    logger = logging.getLogger("Main")
    
    config = GameConfig(
        FIXED_TIMESTEP=args.timestep,
        PREDICTION_WINDOW_MS=args.prediction_window,
        MAX_ROLLBACK_FRAMES=args.max_rollback,
        CLIENT_TICK_RATE=args.tick_rate,
        CORRECTION_SMOOTHING=args.smoothing,
        PLAYER_SPEED=args.player_speed,
        PLAYER_SIZE=args.player_size,
        WORLD_WIDTH=args.world_width,
        WORLD_HEIGHT=args.world_height,
        NETWORK_PORT=args.port,
        DEBUG_VISUALIZATION=not args.headless,
        LOG_LEVEL=args.log_level
    )
    
    logger.info(f"Connecting to {args.host}:{args.port}")
    logger.info(f"  Headless mode: {args.headless}")
    logger.info(f"  Prediction window: {args.prediction_window} ms")
    logger.info(f"  Correction smoothing: {args.smoothing}")
    
    client = GameClient(config, host=args.host)
    
    if not client.connect():
        logger.error("Failed to connect to server")
        sys.exit(1)
    
    visualizer = DebugVisualizer(client, config, headless=args.headless)
    visualizer.start()
    
    try:
        while client.connected:
            time.sleep(0.1)
            
            if args.headless and int(time.time() * 2) % 2 == 0:
                import random
                client.set_input(
                    left=random.random() < 0.3,
                    right=random.random() < 0.3,
                    up=random.random() < 0.3,
                    down=random.random() < 0.3,
                    action=random.random() < 0.1
                )
                
    except KeyboardInterrupt:
        logger.info("Received shutdown signal")
    finally:
        visualizer.stop()
        client.stop()
        logger.info("Client shutdown complete")


def cmd_demo(args):
    setup_logging(args.log_level)
    logger = logging.getLogger("Main")
    
    config = GameConfig(
        FIXED_TIMESTEP=args.timestep,
        PREDICTION_WINDOW_MS=args.prediction_window,
        MAX_ROLLBACK_FRAMES=args.max_rollback,
        CORRECTION_SMOOTHING=args.smoothing,
        LOG_LEVEL=args.log_level
    )
    
    logger.info("=" * 60)
    logger.info("ROLLBACK NETCODE DEMO")
    logger.info("=" * 60)
    logger.info("")
    logger.info("This demo shows the Rollback Netcode components working together.")
    logger.info("")
    logger.info("COMPONENTS:")
    logger.info("  1. Deterministic Physics Engine")
    logger.info("     - Fixed timestep: 1/60s")
    logger.info("     - Deterministic random number generator")
    logger.info("     - AABB collision detection and response")
    logger.info("")
    logger.info("  2. Input Prediction")
    logger.info("     - Client predicts movement immediately on input")
    logger.info("     - Input sent to server simultaneously")
    logger.info(f"     - Prediction window: {args.prediction_window}ms")
    logger.info("")
    logger.info("  3. Rollback & Resimulation")
    logger.info("     - Server calculates authoritative state")
    logger.info("     - Client receives correction packets")
    logger.info("     - If prediction differs, rollback and replay")
    logger.info(f"     - Max rollback: {args.max_rollback} frames")
    logger.info("")
    logger.info("  4. Smooth Correction")
    logger.info("     - Small errors: smoothed interpolation")
    logger.info(f"     - Smoothing factor: {args.smoothing}")
    logger.info("     - Large errors: immediate rollback")
    logger.info("")
    logger.info("  5. Debug Visualization")
    logger.info("     - Red filled circle: predicted position")
    logger.info("     - White outline: authoritative position")
    logger.info("     - Yellow line: prediction error")
    logger.info("     - Blue trail: movement history")
    logger.info("")
    logger.info("USAGE:")
    logger.info("  Terminal 1: python main.py server")
    logger.info("  Terminal 2: python main.py client")
    logger.info("")
    logger.info("Controls: WASD or Arrow Keys to move")
    logger.info("=" * 60)


def main():
    parser = argparse.ArgumentParser(
        description='Rollback Netcode - Input Prediction and Client Compensation',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Start server
  %(prog)s server
  
  # Start client with pygame visualization
  %(prog)s client --host 127.0.0.1
  
  # Start client in headless mode
  %(prog)s client --headless
  
  # Custom configuration
  %(prog)s server --port 5555 --tick-rate 60 --prediction-window 100
  
  # Show demo explanation
  %(prog)s demo
        """
    )
    
    subparsers = parser.add_subparsers(dest='command', help='Available commands')
    
    server_parser = subparsers.add_parser('server', help='Start the game server')
    server_parser.add_argument('--host', default='0.0.0.0', help='Host to bind to')
    server_parser.add_argument('--port', type=int, default=DEFAULT_CONFIG.NETWORK_PORT, help='Port to listen on')
    server_parser.add_argument('--tick-rate', type=int, default=DEFAULT_CONFIG.SERVER_TICK_RATE, help='Server tick rate (Hz)')
    server_parser.add_argument('--timestep', type=float, default=DEFAULT_CONFIG.FIXED_TIMESTEP, help='Fixed timestep (seconds)')
    server_parser.add_argument('--prediction-window', type=int, default=DEFAULT_CONFIG.PREDICTION_WINDOW_MS, help='Prediction window (ms)')
    server_parser.add_argument('--max-rollback', type=int, default=DEFAULT_CONFIG.MAX_ROLLBACK_FRAMES, help='Maximum rollback frames')
    server_parser.add_argument('--player-speed', type=float, default=DEFAULT_CONFIG.PLAYER_SPEED, help='Player movement speed')
    server_parser.add_argument('--player-size', type=float, default=DEFAULT_CONFIG.PLAYER_SIZE, help='Player size')
    server_parser.add_argument('--world-width', type=float, default=DEFAULT_CONFIG.WORLD_WIDTH, help='World width')
    server_parser.add_argument('--world-height', type=float, default=DEFAULT_CONFIG.WORLD_HEIGHT, help='World height')
    server_parser.add_argument('--log-level', default='INFO', choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'], help='Logging level')
    
    client_parser = subparsers.add_parser('client', help='Start the game client')
    client_parser.add_argument('--host', default='127.0.0.1', help='Server host')
    client_parser.add_argument('--port', type=int, default=DEFAULT_CONFIG.NETWORK_PORT, help='Server port')
    client_parser.add_argument('--tick-rate', type=int, default=DEFAULT_CONFIG.CLIENT_TICK_RATE, help='Client tick rate (Hz)')
    client_parser.add_argument('--timestep', type=float, default=DEFAULT_CONFIG.FIXED_TIMESTEP, help='Fixed timestep (seconds)')
    client_parser.add_argument('--prediction-window', type=int, default=DEFAULT_CONFIG.PREDICTION_WINDOW_MS, help='Prediction window (ms)')
    client_parser.add_argument('--max-rollback', type=int, default=DEFAULT_CONFIG.MAX_ROLLBACK_FRAMES, help='Maximum rollback frames')
    client_parser.add_argument('--smoothing', type=float, default=DEFAULT_CONFIG.CORRECTION_SMOOTHING, help='Correction smoothing factor (0-1)')
    client_parser.add_argument('--player-speed', type=float, default=DEFAULT_CONFIG.PLAYER_SPEED, help='Player movement speed')
    client_parser.add_argument('--player-size', type=float, default=DEFAULT_CONFIG.PLAYER_SIZE, help='Player size')
    client_parser.add_argument('--world-width', type=float, default=DEFAULT_CONFIG.WORLD_WIDTH, help='World width')
    client_parser.add_argument('--world-height', type=float, default=DEFAULT_CONFIG.WORLD_HEIGHT, help='World height')
    client_parser.add_argument('--headless', action='store_true', help='Run without pygame visualization')
    client_parser.add_argument('--log-level', default='INFO', choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'], help='Logging level')
    
    demo_parser = subparsers.add_parser('demo', help='Show demo explanation')
    demo_parser.add_argument('--timestep', type=float, default=DEFAULT_CONFIG.FIXED_TIMESTEP, help='Fixed timestep (seconds)')
    demo_parser.add_argument('--prediction-window', type=int, default=DEFAULT_CONFIG.PREDICTION_WINDOW_MS, help='Prediction window (ms)')
    demo_parser.add_argument('--max-rollback', type=int, default=DEFAULT_CONFIG.MAX_ROLLBACK_FRAMES, help='Maximum rollback frames')
    demo_parser.add_argument('--smoothing', type=float, default=DEFAULT_CONFIG.CORRECTION_SMOOTHING, help='Correction smoothing factor')
    demo_parser.add_argument('--log-level', default='INFO', choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'], help='Logging level')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        sys.exit(1)
    
    try:
        if args.command == 'server':
            cmd_server(args)
        elif args.command == 'client':
            cmd_client(args)
        elif args.command == 'demo':
            cmd_demo(args)
    except KeyboardInterrupt:
        print("\n[!] Interrupted by user")
        sys.exit(0)
    except Exception as e:
        logging.error(f"Fatal error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
