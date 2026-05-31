# MongoDB to ClickHouse Sync Tool

A real-time data synchronization tool that replicates data from MongoDB to ClickHouse using MongoDB Change Streams.

## Features

- Real-time sync via MongoDB Change Streams
- Initial full sync support
- ReplacingMergeTree for eventual consistency
- Field type mapping (BSON to ClickHouse)
- Conflict resolution strategies
- Resume token support for fault tolerance
- Multi-table sync
- Lag monitoring
- Status monitoring
- **Cluster Election Resilience** - Automatic failover handling
- **Multi-node Fallback** - Automatic fallback to secondary nodes
- **Local Disk Cache** - Up to 1GB event caching during recovery
- **Batch Catchup** - Automatic batch processing after recovery
- **Election Alerts** - Alert system for cluster elections
- **Manual/Auto Resume Policy** - Configurable resume strategy
- **ETL Pipeline** - Real-time data transformation
- **JavaScript/Wasm Scripts** - Custom transformation logic
- **Hot Reload** - Update pipelines without restarting
- **Web Debug Console** - Test transformations with sample data
- **Dead Letter Queue** - Kafka integration for failed transforms

## Usage

### Initial Sync

```bash
./mongochsync --config config.yaml --initial-sync
```

### Real-time Sync

```bash
./mongochsync --config config.yaml
```

### Check Status

```bash
./mongochsync --config config.yaml --status
```

### Manual Resume (for manual resume policy)

```bash
./mongochsync --config config.yaml --resume
```

### Hot Reload Pipelines

```bash
./mongochsync --config config.yaml --reload-pipelines
```

## Configuration

Edit `config.yaml` to set up:

- MongoDB connection details
- ClickHouse connection details
- Table mappings
- Sync settings (batch size, timeout, etc.)
- **Resilience settings** (resume policy, cache, etc.)
- **ETL settings** (pipelines, DLQ, debug server)

### Resilience Configuration

```yaml
resilience:
  resume_policy: "auto"      # auto or manual
  max_reconnect: 10          # max reconnection attempts
  cache_dir: "./cache"       # local cache directory
  cache_max_size_mb: 1024    # max cache size (1GB)
  election_timeout_ms: 5000  # election detection timeout
```

### Replica Set Members

```yaml
mongodb:
  replica_set_members:
    - uri: "mongodb://localhost:27017"
      name: "primary"
    - uri: "mongodb://localhost:27018"
      name: "secondary1"
    - uri: "mongodb://localhost:27019"
      name: "secondary2"
```

### ETL Configuration

```yaml
etl:
  enabled: true
  pipeline_file: "./pipelines.yaml"
  debug_server:
    enabled: true
    addr: ":8081"
  dlq:
    type: "kafka"
    brokers:
      - "localhost:9092"
    topic: "etl_dlq"
    buffer_size: 1000
```

## ETL Pipeline

### Pipeline Definition (pipelines.yaml)

```yaml
pipelines:
  - name: "flatten_users"
    stages:
      - type: "filter"
        name: "filter_active"
        config:
          script: |
            var status = doc.status || '';
            return status === 'active' || status === 'premium';

      - type: "transform"
        name: "flatten_address"
        config:
          script: |
            var result = Object.assign({}, doc);
            if (doc.address && typeof doc.address === 'object') {
              for (var key in doc.address) {
                result['address_' + key] = doc.address[key];
              }
              delete result.address;
            }
            result.age = new Date().getFullYear() - (doc.birth_year || 0);
            return result;

      - type: "project"
        name: "select_fields"
        config:
          fields:
            - "name"
            - "email"
            - "age"
            - "address_city"
            - "address_country"

      - type: "convert"
        name: "type_conversion"
        config:
          script: |
            var result = Object.assign({}, doc);
            if (result.age) {
              result.age = parseInt(result.age, 10);
            }
            return result;
```

### Stage Types

1. **filter** - Filter documents based on conditions
2. **transform** - Apply transformations (flatten, derive fields)
3. **project** - Select/rename fields
4. **convert** - Type conversions
5. **wasm** - Custom Wasm transformations

### Table Schema in ClickHouse

For each target table, create it as ReplacingMergeTree:

```sql
CREATE TABLE users (
    id String,
    name String,
    email String,
    age Int64,
    city String,
    country String,
    created_at DateTime,
    version Int64
) ENGINE = ReplacingMergeTree(version)
ORDER BY id;
```

## Building

```bash
go mod download
go build -o mongochsync
```

## Resume Token Invalidation Handling

When MongoDB cluster performs a primary election:

1. **Detection**: The tool detects when the primary becomes unavailable
2. **Caching**: During reconnection, events are cached to local disk (up to 1GB)
3. **Fallback**: Attempts to resume from secondary nodes if primary's token is invalid
4. **Recovery**: After finding a valid node, processes cached events in batch
5. **Alert**: Election events are logged for monitoring

### Resume Policies

- **auto** (default): Automatically recovers from elections
- **manual**: Waits for explicit resume command via `--resume` flag

### Alert System

The tool logs election events with details:

```
[ELECTION ALERT] election_detected
{
  "event_type": "election_detected",
  "timestamp": "2024-01-01T00:00:00Z",
  "recovery_strategy": "automatic"
}

[RECOVERY ALERT] Table users recovered, processed 150 cached events

[FALLBACK ALERT] Switching from primary to secondary1
```

## Web Debug Console

When ETL is enabled, access the debug console at `http://localhost:8081`:

- Test pipeline transformations with sample BSON input
- View pipeline statistics
- Monitor latency (target: <1ms per document)
- View recent DLQ messages

## Performance

- **Transformation Latency**: <1ms per document (target)
- **Batch Processing**: Configurable batch size and timeout
- **DLQ**: Kafka integration with buffered writes

## Project Structure

```
e:\trae1\103/
├── etl/
│   ├── pipeline.go        # Core pipeline engine
│   ├── loader.go          # Pipeline config loader
│   ├── debug_server.go     # Web debug console
│   ├── javascript/
│   │   └── engine.go      # JS script execution
│   └── wasm/
│       └── engine.go       # Wasm execution
├── dlq/
│   └── kafka.go           # Kafka DLQ integration
├── resilience/
│   └── resilience.go      # Election handling
├── config/
│   └── config.go          # Configuration
├── syncer/
│   └── syncer.go          # Core sync engine
└── main.go                # Entry point
```
