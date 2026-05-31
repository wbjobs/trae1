# Performance Tuning Guide

## System-level Optimizations

### 1. Increase File Descriptor Limits

```bash
# Temporary
ulimit -n 65535

# Permanent (add to /etc/security/limits.conf)
* soft nofile 65535
* hard nofile 65535
```

### 2. TCP Tuning

```bash
# Add to /etc/sysctl.conf
net.core.rmem_max = 134217728
net.core.wmem_max = 134217728
net.ipv4.tcp_rmem = 4096 87380 67108864
net.ipv4.tcp_wmem = 4096 65536 67108864
net.ipv4.tcp_congestion_control = bbr
net.core.netdev_max_backlog = 5000
net.ipv4.tcp_max_syn_backlog = 8192
net.core.somaxconn = 8192
```

### 3. CPU Affinity

```bash
# Pin to specific CPU cores
taskset -c 0-7 ./target/release/esni-gateway
```

## Application-level Optimizations

### 1. Worker Threads

Set worker count to match CPU cores:
```bash
./target/release/esni-gateway --workers $(nproc)
```

### 2. Buffer Size

Increase for high-throughput scenarios:
```bash
./target/release/esni-gateway --buffer-size 32768
```

### 3. Connection Pooling

Enable connection pooling for backend connections (in code):
```rust
let pool = ConnectionPool::new(100, Duration::from_secs(300));
```

## Monitoring

### Prometheus Metrics

Key metrics to monitor:
- `esni_connections_active` - Active connections
- `esni_request_duration_seconds` - Latency
- `esni_sni_parse_success` - Parse success rate

### Grafana Dashboard

Import the provided dashboard for real-time monitoring.

## Benchmarking

### Using wrk

```bash
wrk -t4 -c1000 -d30s https://localhost:443/
```

### Using hey

```bash
hey -z 30s -c 1000 https://localhost:443/
```

## Expected Performance

- **Connections per second**: 500,000+ cps
- **Latency (p99)**: < 10ms
- **Memory per connection**: ~10KB
- **CPU utilization**: Linear scaling with cores

## Troubleshooting

### High CPU Usage

1. Check SNI parsing efficiency
2. Verify no excessive logging
3. Review DNSSEC verification overhead

### High Memory Usage

1. Reduce buffer sizes
2. Check for connection leaks
3. Monitor connection pool size

### Low Throughput

1. Check network bandwidth
2. Verify backend server performance
3. Review TLS handshake overhead
