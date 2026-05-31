# 🦀 Rust Web Framework Benchmark Report

Generated: 2026-05-26 10:32:49

## Configuration

| Parameter | Value |
|-----------|-------|
| Concurrency | 10 |
| Duration | 5s |
| Runs per test | 1x |
| Timeout | 300s |
| Frameworks Tested | Axum |
| Scenarios Tested | JSON Serialization |

## ⚠️ Run Status

- **Axum**: `Build Failed` - Reason: Spawn failed: Binary not found: \\?\E:\trae1\120\target\release\axum-server. Run cargo build --release -p axum-server first. | Peak Memory: 0.0 MB | 2026-05-26 10:32:46

## 🔥 CPU Profiling & Hotspot Analysis

Flamegraphs are saved in the `flamegraphs/` directory.

### JSON Serialization - Hotspot Summary

## 🔬 JSON Serialization

| Framework | QPS (avg ± std) | P50 (ms) | P95 (ms) | P99 (ms) | Success | CPU % | Memory (MB) |
|-----------|-----------------|----------|----------|----------|---------|-------|-------------|
| Axum | - | - | - | - | - | - | - | (Build Failed) |

## Notes

- All tests run in release mode with `opt-level=3`, `lto=fat`, `codegen-units=1`
- QPS = Queries Per Second, higher is better
- P50/P95/P99 = Latency percentiles, lower is better
- CPU and Memory usage are averages sampled during the test
- Timeout: Each framework test is limited to the configured timeout (default 300s)
- If a framework crashes or times out, it is marked in the results and skipped
