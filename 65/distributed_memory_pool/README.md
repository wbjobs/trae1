# Distributed Memory Pool (DMP)

A high-performance distributed shared memory pool service using RDMA (RoCEv2) for inter-node communication.

## Features

- **Distributed Shared Memory**: 64GB total capacity, shared across multiple nodes
- **RDMA Acceleration**: Uses libverbs for RDMA READ/WRITE operations, bypassing CPU
- **Slab Allocator**: Efficient memory management with 8 size classes (4KB - 1MB)
- **gRPC Interface**: Standard RPC for allocate/release/PUT/GET operations
- **Fault Tolerance**: Automatic memory recovery on node failure
- **Lease-based Block Management**: Prevents memory leaks with time-based block leasing
- **Monitoring**: Real-time memory usage and fragmentation statistics

## Architecture

```
+------------------+         gRPC          +------------------+
|                  |   ----------------->  |                  |
|  DMP Client      |                       |  DMP Server      |
|                  |   <-----------------  |                  |
+------------------+                       +------------------+
       |                                            |
       | RDMA READ/WRITE                            |
       v                                            v
+---------------------------------------------------------------+
|                    RDMA Network (RoCEv2)                      |
+---------------------------------------------------------------+
```

## Requirements

- Linux with RDMA support (RoCEv2)
- libibverbs (RDMA library)
- gRPC and Protobuf
- C++17 compiler
- CMake 3.14+
- Mellanox or compatible RDMA NIC

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Build Options

```bash
# Build with tests
cmake .. -DBUILD_TESTS=ON

# Debug build
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

## Configuration

Create a configuration file (e.g., `server.conf`):

```ini
node_id = 1
grpc_address = 0.0.0.0
grpc_port = 50051
rdma_device = mlx5_0
rdma_port = 1
rdma_gid_index = 0
total_memory = 64G
use_hugepages = false
heartbeat_interval_ms = 1000
heartbeat_timeout_ms = 5000
lease_duration_ms = 30000
```

## Running

### Start Server

```bash
# Using command line arguments
./dmp_server --node_id 1 --port 50051 --rdma_device mlx5_0 --memory 64G

# Using configuration file
./dmp_server --config server.conf

# With monitoring
./dmp_server --config server.conf --log_level INFO
```

### Start Client

```bash
./dmp_client --server 192.168.1.100 --port 50051 --node_id 100
```

### Command Line Options

**Server Options:**
- `--config <path>` - Path to configuration file
- `--node_id <id>` - Node ID (unique per node)
- `--port <port>` - gRPC port (default: 50051)
- `--rdma_device <dev>` - RDMA device name (default: mlx5_0)
- `--rdma_port <port>` - RDMA port (default: 1)
- `--memory <size>` - Total memory size (e.g., 64G, 512M)
- `--hugepages` - Use huge pages
- `--log_level <level>` - Log level (DEBUG, INFO, WARN, ERROR)

**Client Options:**
- `--server <address>` - Server address (default: localhost)
- `--port <port>` - Server port (default: 50051)
- `--node_id <id>` - Client node ID (default: 100)

## gRPC API

### MemoryService

#### Allocate
Allocate a memory block from the pool.

```protobuf
rpc Allocate(AllocateRequest) returns (AllocateResponse);
```

Request:
- `size` - Requested block size (4KB - 1MB)
- `client_node_id` - Requesting node ID
- `exclusive` - Exclusive access flag

Response:
- `success` - Operation status
- `block` - Allocated block info (block_id, offset, size, state)
- `remote_node_id` - Node hosting the memory
- `remote_address` - Remote address for RDMA access
- `remote_rkey` - Remote key for RDMA operations

#### Release
Release an allocated memory block.

```protobuf
rpc Release(ReleaseRequest) returns (ReleaseResponse);
```

#### Put
Write data to a memory block.

```protobuf
rpc Put(PutRequest) returns (PutResponse);
```

Request:
- `block_id` - Target block ID
- `offset` - Offset within the block
- `data` - Data to write (max 1MB)

#### Get
Read data from a memory block.

```protobuf
rpc Get(GetRequest) returns (GetResponse);
```

Request:
- `block_id` - Source block ID
- `offset` - Offset within the block
- `length` - Number of bytes to read (max 1MB)

#### Monitor
Get memory pool statistics.

```protobuf
rpc Monitor(MonitorRequest) returns (MonitorResponse);
```

Response includes:
- Total/used/free capacity
- Usage percentage
- Fragmentation ratio
- Block count statistics
- Leaked blocks (if detailed=true)

#### Heartbeat
Send node heartbeat.

```protobuf
rpc Heartbeat(HeartbeatRequest) returns (HeartbeatResponse);
```

#### ReportNodeFailure
Report a node failure for memory recovery.

```protobuf
rpc ReportNodeFailure(NodeFailureRequest) returns (NodeFailureResponse);
```

## Memory Size Classes

The slab allocator uses the following size classes:

| Class | Size  |
|-------|-------|
| 0     | 4 KB  |
| 1     | 8 KB  |
| 2     | 16 KB |
| 3     | 32 KB |
| 4     | 64 KB |
| 5     | 128 KB|
| 6     | 256 KB|
| 7     | 1 MB  |

## RDMA Operations

### Setup

1. Each node registers its memory region with the RDMA NIC
2. Nodes exchange connection info (QPN, LID, GID, rkey, address)
3. RDMA queue pairs (QPs) are established between nodes

### Data Transfer

- **RDMA Write**: Client writes data directly to server memory
- **RDMA Read**: Client reads data directly from server memory
- Both operations bypass the remote CPU

## Fault Tolerance

### Node Failure Handling

1. Heartbeat monitor detects failed nodes
2. All blocks owned by the failed node are identified
3. Blocks are automatically released back to the free pool
4. Other nodes can reallocate the recovered memory

### Lease-based Block Management

- Each allocated block has a lease (default: 30 seconds)
- Clients must renew leases via heartbeat or operations
- Expired leases are marked as "fault" and can be recovered

## Monitoring

### Memory Statistics

- **Usage**: Percentage of total capacity in use
- **Fragmentation**: Ratio of total free space to largest free block
- **Blocks**: Count of allocated/free/total blocks

### Accessing Statistics

```bash
# Via gRPC Monitor RPC
# Or via periodic logging (every 10 seconds)
```

## Performance

- Latency: Microsecond-level for RDMA operations
- Throughput: Line-rate limited by NIC
- Block allocation: O(1) via slab free lists
- Max transfer: 1MB per operation

## Limitations

- Requires RDMA-capable NIC
- Linux only
- Memory pool is per-node (distributed but not replicated)
- No data persistence across restarts

## License

MIT License

## Contributing

Contributions welcome! Please submit pull requests.

## Support

For issues and questions, please use the GitHub issue tracker.
