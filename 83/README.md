# VNC Watermark Audit Proxy

A man-in-the-middle VNC proxy that overlays dynamic watermarks on VNC sessions, records all user operations, and provides a web interface for session playback and audit log review.

## Architecture

```
VNC Client ──► Proxy (libvncserver) ──► Watermark Engine ──► libvncclient ──► Real VNC Server
                                  │
                                  ├──► SQLite Logger (keyboard/mouse events)
                                  └──► Session Recorder (PNG screenshots)
```

## Features

- **Dynamic Watermark**: Semi-transparent (20% opacity) watermark showing current time, client IP, and username. Randomly floats to new position every 5 seconds to prevent screenshot evasion.
- **Operation Logging**: All keyboard inputs and mouse clicks are recorded in SQLite database.
- **Session Recording**: Periodic PNG screenshots (every 5 seconds) saved to disk for playback.
- **Web Interface**: Browser-based session viewer with frame-by-frame playback and event log browsing.

## Build Requirements

- C compiler (gcc/clang)
- CMake >= 3.10
- libvncserver / libvncclient
- SQLite3
- libpng
- Node.js >= 16 (for web interface)

### Install dependencies on Ubuntu/Debian

```bash
sudo apt install build-essential cmake libvncserver-dev libvncclient-dev libsqlite3-dev libpng-dev nodejs npm
```

### Install dependencies on macOS

```bash
brew install cmake libvncserver sqlite libpng node
```

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Usage

### Start the Proxy

```bash
./vnc_proxy \
  --server 192.168.1.100 \
  --port 5900 \
  --password "vnc_password" \
  --listen 5901 \
  --user "admin" \
  --db data/audit.db \
  --record recordings
```

### Command Line Options

| Option | Default | Description |
|--------|---------|-------------|
| `-s, --server` | 127.0.0.1 | Real VNC server host |
| `-p, --port` | 5900 | Real VNC server port |
| `-w, --password` | (none) | Real VNC server password |
| `-l, --listen` | 5901 | Proxy listen port for VNC clients |
| `-u, --user` | anonymous | Username for audit logging |
| `-d, --db` | data/audit.db | SQLite database path |
| `-r, --record` | recordings | Screenshot recording directory |

### Start the Web Interface

```bash
cd web
npm install
npm start
```

Then open http://localhost:3000 in your browser.

## Connecting a VNC Client

Point your VNC client (e.g., TigerVNC, RealVNC, TightVNC) to the proxy address and port:

```
vncviewer localhost:5901
```

## Database Schema

- **sessions**: Session metadata (client IP, username, server, timestamps)
- **keyboard_events**: Key press/release events with key codes
- **mouse_events**: Mouse position and button events
- **screenshots**: File paths of recorded screenshots linked to sessions

## Directory Structure

```
├── src/
│   ├── main.c          # Entry point, argument parsing
│   ├── proxy.c/.h      # Core VNC proxy (libvncserver ↔ libvncclient bridge)
│   ├── watermark.c/.h  # Watermark rendering engine
│   ├── logger.c/.h     # SQLite audit logger
│   └── recorder.c/.h   # PNG screenshot recorder
├── web/
│   ├── server.js       # Express web server with REST API
│   ├── package.json
│   └── public/
│       └── index.html  # Frontend (session viewer, playback, logs)
├── data/               # SQLite database directory
├── recordings/         # PNG screenshots directory
└── CMakeLists.txt
```

## Technical Details

### Watermark Rendering

The watermark uses a built-in 5x7 bitmap font and alpha blending at 20% opacity (alpha = 51). The watermark text format is:

```
YYYY-MM-DD HH:MM:SS | IP:192.168.1.100 | User:admin
```

The position changes every 5 seconds to a random location within screen bounds, making it harder to remove the watermark from screenshots.

### RFB Protocol Handling

- The proxy acts as a VNC server (libvncserver) to accept client connections
- Simultaneously acts as a VNC client (libvncclient) to the real server
- Framebuffer updates from the real server are intercepted, watermark is applied, and forwarded to proxy clients
- Keyboard/mouse events from proxy clients are logged then forwarded to the real server
