# Real-Time Risk Control Platform

A real-time risk control solution with **Redis Cell (CL.THROTTLE)** for sub-millisecond rate limiting, a Lua-based blacklist check, gRPC for ultra-low-latency decisions, Kafka-based decision logging, and a React + ECharts dashboard.

## Architecture

```
Client ──gRPC──▶ Risk Control Service ──CL.THROTTLE──▶ Redis (Redis Cell)
                      │                              └─Lua─▶ Redis (blacklist)
                      └──async──▶ Kafka ──▶ Dashboard Backend ──▶ Dashboard Frontend
```

## Strategy pipeline (ordered)

1. **Blacklist** — Lua script (O(1)) checks global + per-action Redis SETs. On hit → **REJECT**.
2. **Rate Limit** — `CL.THROTTLE` (C-level GCRA, <1ms). On throttle → **REJECT**; at ≤20% remaining → **VERIFY**.
3. Otherwise → **PASS**.

Only **two** Redis calls per request. The rate-limit call uses Redis Cell's native `CL.THROTTLE` command (GCRA algorithm implemented in C), which handles 100k+ req/s per key without blocking.

### Why Redis Cell instead of Lua-based sliding window / token bucket?

The original Lua scripts used:
- **ZSET sliding window**: `ZREMRANGEBYSCORE` + `ZCARD` are O(log N) per call. At 1000 req/s with a 60s window, N = 60,000 and a single call can take 5–20 ms, blocking Redis for all other tenants.
- **HASH token bucket**: `HMGET` + `HMSET` in Lua. While faster, still involves multiple hash operations and a Lua interpreter overhead per call.

**Redis Cell (`CL.THROTTLE`)** implements the Generic Cell Rate Algorithm (GCRA) in pure C. Every call is O(1), allocation-free, and consistently <1 ms regardless of request volume.

### Fallback: fixed-window Lua counter

If Redis Cell is not installed (e.g. vanilla Redis), the engine automatically falls back to `lua/fixed_window.lua` — a minimal `INCR` + `EXPIRE` script that is also O(1) and <1 ms. This trades some precision (fixed window vs. GCRA) for guaranteed sub-millisecond execution.

## Repository layout

```
risk-control-service/    gRPC + Redis + Kafka service (Node.js)
├── proto/               gRPC .proto definitions
├── lua/
│   ├── blacklist.lua     Active — O(1) SET lookup
│   ├── fixed_window.lua  Active — fallback when Redis Cell unavailable
│   ├── sliding_window.lua [DEPRECATED] — O(log N), blocked at high QPS
│   └── token_bucket.lua   [DEPRECATED] — replaced by Redis Cell
├── src/                 Server, engine, redis, kafka modules
└── scripts/             CLI client & benchmark
dashboard-backend/       Kafka consumer → Redis aggregation → HTTP API
dashboard-frontend/      React + ECharts dashboard
docker-compose.yml       Redis + Kafka + Zookeeper
```

## Enabling Redis Cell

The official `redis-cell` module is available at https://github.com/brandur/redis-cell. Load it in `redis.conf`:

```
loadmodule /path/to/redis-cell/target/release/libredis_cell.so
```

Or with Docker, use an image that bundles Redis Cell, e.g.:

```yaml
redis:
  image: redislabs/redis-cell:latest  # or build your own
```

The service auto-detects Redis Cell on startup via `MODULE LIST`. If absent, it logs a warning and uses the fixed-window Lua fallback.

## Decisions

| Decision | Meaning |
|----------|---------|
| PASS     | No strategy triggered. |
| REJECT   | Hard block (blacklist or rate limit exceeded). |
| VERIFY   | Soft block (near rate limit) — user should be challenged (CAPTCHA, MFA, etc.). |

## Quick start

```bash
# 1. Start infrastructure (Redis + Kafka + Zookeeper)
docker compose up -d

# 2. Install service deps & run
cd risk-control-service && npm install && npm start
# gRPC listening on 0.0.0.0:50051

# 3. Install & run dashboard backend
cd dashboard-backend && npm install && npm start
# HTTP API on 0.0.0.0:4000

# 4. Install & run dashboard frontend
cd dashboard-frontend && npm install && npm run dev
# Web UI on http://localhost:5173
```

## gRPC API

```proto
service RiskControlService {
  rpc Check(CheckRequest) returns (CheckResponse);
  rpc CheckStream(stream CheckRequest) returns (stream CheckResponse);
}
```

### Example request

```js
{ user_id: "u-123", action_type: "login", timestamp: 1730000000000 }
```

### Example response

```js
{
  decision: "PASS",
  hit_strategy: "NONE",
  reason: "ok",
  request_id: 42,
  latency_ms: 0.42
}
```

## CLI client

```bash
node scripts/client.js <user_id> <action_type> [count]
node scripts/bench.js          # 100-req benchmark across 3 scenarios
```

## Dashboard API

- `GET /api/health`
- `GET /api/metrics?window=60`  — returns summary, per-minute series, strategy distribution.

## Tuning

| Env var | Default | Purpose |
|---------|---------|---------|
| `GRPC_PORT` | 50051 | gRPC server port |
| `REDIS_HOST` / `REDIS_PORT` | 127.0.0.1:6379 | Redis |
| `KAFKA_BROKERS` | localhost:9092 | Kafka bootstrap servers |
| `KAFKA_TOPIC` | risk-control-decisions | Decision log topic |

Per-action rate-limit parameters live in `src/config.js` under `strategies.rateLimit.defaults`. Each action has a `maxBurst`, `countPerPeriod`, and `periodSeconds` triple that maps directly to `CL.THROTTLE` arguments.

## Performance target

Each `Check` call runs at most **two** O(1) Redis commands: one Lua blacklist check + one `CL.THROTTLE` (or fixed-window fallback). On typical local hardware this completes in **<1 ms p50 and <3 ms p99**. Latency is measured per-request via `process.hrtime.bigint()` and returned in `latency_ms`. Kafka publishing is deferred via `setImmediate` so it never contributes to the decision latency.
