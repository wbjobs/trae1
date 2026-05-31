# Distributed Compute Mesh

A browser-based distributed computing coordination system. Multiple browser tabs form a
P2P computing network over **WebRTC DataChannels**. A **Master** node shards compute tasks
(Fibonacci, matrix multiplication, prime sieve) and distributes them to **Worker** nodes
in parallel, then aggregates and displays the final result.

## Architecture

```
┌─────────────────────┐    WebSocket (signaling)    ┌─────────────────────┐
│   Node.js Server    │ ◄──────────────────────────► │   Browser Clients   │
│  (ws + express)     │                              │  Master / Workers   │
└─────────────────────┘                              └─────────────────────┘
                                                            │
                                                            ▼
                                                  ┌─────────────────────┐
                                                  │ WebRTC P2P DataChan │
                                                  └─────────────────────┘
```

- **Signaling server** (`server/`) – Node.js + `ws` + `express`. Relays SDP/ICE candidates
  and keeps a registry of connected nodes.
- **Frontend** (`frontend/`) – React 18 + Vite. A tab can join as either **Master** or
  **Worker**.
- **Master** – creates task chunks, opens one `RTCDataChannel` to each Worker, assigns
  chunks in a pull-fashion (a free worker gets the next chunk), and aggregates results.
- **Worker** – accepts DataChannel connections from Masters, runs a chunk locally, and
  returns the result + measured duration.
- Tasks supported:
  - Fibonacci sequence (big-`n` iteration)
  - Dense matrix multiplication (A × B)
  - Prime sieve over a large range

## Run

### 1. Install dependencies

```bash
# Server
cd server
npm install

# Frontend
cd ../frontend
npm install
```

### 2. Start the signaling server

```bash
cd server
npm start
# listens on http://localhost:4000 (ws at /ws)
```

### 3. Start the frontend

```bash
cd frontend
npm run dev
# open http://localhost:5173
```

### 4. Form the mesh

- Open `http://localhost:5173` in **one** tab, select **I am Master**.
- Open the same URL in **several other** tabs (or on other machines in the LAN), select
  **I am Worker** in each.
- In the Master tab, click **Connect All Workers**, select a task, click **▶ Run Task**.
- The UI shows per-worker status, live progress bar, task duration, and the aggregated
  result.

## Files

```
server/
  server.js              Express + WS signaling server
  package.json
frontend/
  src/
    App.jsx              Role picker + router
    main.jsx
    styles.css
    components/
      MasterView.jsx     Master UI: task form, progress, results, peer list
      WorkerView.jsx     Worker UI: current task, recent results, log
      NodeList.jsx       Per-peer status card
      LogConsole.jsx     Shared log viewer
    utils/
      signaling.js       WebSocket client wrapper
      webrtc.js          RTCPeerConnection / DataChannel helpers
      tasks.js           Task generators + chunk execution + aggregation
      MasterNode.js      Master-side peer manager + task dispatcher
      WorkerNode.js      Worker-side peer manager + task executor
      utils.js
  vite.config.js
  package.json
```

## Notes

- WebRTC requires a signaling channel before P2P links can form; the Node.js server only
  relays small control messages — all compute data flows over DataChannels directly.
- For cross-machine setups, replace `ws://localhost:4000` with the server's LAN IP in the
  join screen.
- The prime sieve and matrix tasks are intentionally CPU-heavy so parallelism is visible.
