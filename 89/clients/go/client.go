// Package columnar provides a Go client for the Arrow columnar data gateway.
//
// The gateway speaks gRPC + Arrow IPC (File for upload, Stream for
// download/query). This client uses the pure-Go Arrow implementation,
// which keeps columnar memory layouts identical to those produced by
// Python / Rust. As a consequence, a dataset produced by a Python
// pandas DataFrame can be downloaded directly into Go []arrow.Array
// slices without any (de)serialization beyond the Arrow IPC framing.
package columnar

import (
	"bytes"
	"context"
	"fmt"
	"io"

	"github.com/apache/arrow/go/v18/arrow"
	"github.com/apache/arrow/go/v18/arrow/array"
	"github.com/apache/arrow/go/v18/arrow/ipc"
	"github.com/apache/arrow/go/v18/arrow/memory"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	pb "github.com/columnar-gateway/go-client/proto/columnar/gateway"
)

// Client 连接到 columnar-gateway。
type Client struct {
	conn  *grpc.ClientConn
	stub  pb.ColumnarGatewayClient
	alloc memory.Allocator
}

// ClientOption 配置 Client。
type ClientOption func(*Client)

// WithAllocator 注入自定义 Arrow 内存分配器（默认 memory.DefaultAllocator）。
func WithAllocator(a memory.Allocator) ClientOption {
	return func(c *Client) { c.alloc = a }
}

// New 连接到 target (host:port)。
func New(ctx context.Context, target string, opts ...ClientOption) (*Client, error) {
	conn, err := grpc.NewClient(
		target,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithDefaultCallOptions(
			grpc.MaxCallSendMsgSize(2*1024*1024*1024),
			grpc.MaxCallRecvMsgSize(2*1024*1024*1024),
		),
	)
	if err != nil {
		return nil, err
	}
	c := &Client{
		conn:  conn,
		stub:  pb.NewColumnarGatewayClient(conn),
		alloc: memory.DefaultAllocator,
	}
	for _, o := range opts {
		o(c)
	}
	return c, nil
}

// Close 关闭底层 gRPC 连接。
func (c *Client) Close() error { return c.conn.Close() }

// ---------------------------------------------------------------- upload

// Upload 上传一个 *arrow.Table 作为数据集的新版本。
//
// 若数据来自 Python pandas，Python 端以 Arrow IPC File 格式发送；
// Go 端接收到的 Record 列表与原始 DataFrame 在内存中的列式布局一致。
func (c *Client) Upload(ctx context.Context, datasetName string, tbl arrow.Table) (*pb.DatasetInfo, error) {
	buf := &bytes.Buffer{}
	w := ipc.NewFileWriter(buf, ipc.WithSchema(tbl.Schema()), ipc.WithAllocator(c.alloc))
	tr := array.NewTableReader(tbl, 64*1024)
	defer tr.Release()
	for tr.Next() {
		rec := tr.Record()
		if err := w.Write(rec); err != nil {
			w.Close()
			return nil, fmt.Errorf("write: %w", err)
		}
	}
	if err := w.Close(); err != nil {
		return nil, fmt.Errorf("close writer: %w", err)
	}

	raw := buf.Bytes()
	const chunk = 64 * 1024 * 1024
	stream, err := c.stub.Upload(ctx)
	if err != nil {
		return nil, err
	}
	first := true
	for i := 0; i < len(raw); i += chunk {
		end := i + chunk
		if end > len(raw) {
			end = len(raw)
		}
		slice := make([]byte, end-i)
		copy(slice, raw[i:end])
		name := ""
		if first {
			name = datasetName
			first = false
		}
		if err := stream.Send(&pb.UploadRequest{
			DatasetName: name,
			ArrowIpc:    slice,
		}); err != nil {
			return nil, fmt.Errorf("send: %w", err)
		}
	}
	resp, err := stream.CloseAndRecv()
	if err != nil {
		return nil, err
	}
	return resp.Info, nil
}

// ---------------------------------------------------------------- download

// Download 读取指定版本（0 表示最新），返回 arrow.Table。
func (c *Client) Download(ctx context.Context, datasetName string, version int64) (arrow.Table, error) {
	stream, err := c.stub.Download(ctx, &pb.DownloadRequest{
		DatasetName: datasetName,
		Version:     version,
	})
	if err != nil {
		return nil, err
	}
	return readDownloadStream(stream, c.alloc)
}

// ---------------------------------------------------------------- query

// Query 以 DataFusion SQL 方言执行查询并返回 arrow.Table。
//
// 支持:
//   SELECT col1, col2 FROM t WHERE col3 > 10
//   SELECT col1, col2 WHERE col3 > 10   (FROM t 由服务端自动补全)
func (c *Client) Query(ctx context.Context, datasetName, sql string, version int64) (arrow.Table, error) {
	stream, err := c.stub.Query(ctx, &pb.QueryRequest{
		DatasetName: datasetName,
		Sql:         sql,
		Version:     version,
	})
	if err != nil {
		return nil, err
	}
	return readQueryStream(stream, c.alloc)
}

// ---------------------------------------------------------------- meta

// ListDatasets 返回所有数据集名。
func (c *Client) ListDatasets(ctx context.Context) ([]string, error) {
	resp, err := c.stub.ListDatasets(ctx, &pb.ListDatasetsRequest{})
	if err != nil {
		return nil, err
	}
	return resp.Names, nil
}

// ListVersions 返回指定数据集的所有版本元信息。
func (c *Client) ListVersions(ctx context.Context, datasetName string) ([]*pb.DatasetInfo, error) {
	resp, err := c.stub.ListVersions(ctx, &pb.ListVersionsRequest{DatasetName: datasetName})
	if err != nil {
		return nil, err
	}
	return resp.Versions, nil
}

// Delete 删除指定数据集版本；version=0 删除整个数据集。
func (c *Client) Delete(ctx context.Context, datasetName string, version int64) (bool, error) {
	resp, err := c.stub.Delete(ctx, &pb.DeleteRequest{
		DatasetName: datasetName,
		Version:     version,
	})
	if err != nil {
		return false, err
	}
	return resp.Ok, nil
}

// ---------------------------------------------------------------- internal

func readDownloadStream(stream pb.ColumnarGateway_DownloadClient, alloc memory.Allocator) (arrow.Table, error) {
	var records []arrow.Record
	var schema *arrow.Schema
	for {
		msg, err := stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, err
		}
		if len(msg.ArrowIpc) == 0 {
			continue
		}
		if err := appendRecords(&records, &schema, msg.ArrowIpc, alloc); err != nil {
			return nil, err
		}
	}
	return buildTable(schema, records)
}

func readQueryStream(stream pb.ColumnarGateway_QueryClient, alloc memory.Allocator) (arrow.Table, error) {
	var records []arrow.Record
	var schema *arrow.Schema
	for {
		msg, err := stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, err
		}
		if len(msg.ArrowIpc) == 0 {
			continue
		}
		if err := appendRecords(&records, &schema, msg.ArrowIpc, alloc); err != nil {
			return nil, err
		}
	}
	return buildTable(schema, records)
}

func appendRecords(records *[]arrow.Record, schema **arrow.Schema, ipcBytes []byte, alloc memory.Allocator) error {
	r, err := ipc.NewReader(bytes.NewReader(ipcBytes), ipc.WithAllocator(alloc))
	if err != nil {
		return fmt.Errorf("ipc reader: %w", err)
	}
	defer r.Release()
	if *schema == nil {
		s := r.Schema()
		*schema = &s
	}
	for r.Next() {
		rec := r.Record()
		rec.Retain()
		*records = append(*records, rec)
	}
	return r.Err()
}

func buildTable(schema *arrow.Schema, records []arrow.Record) (arrow.Table, error) {
	if schema == nil {
		return nil, fmt.Errorf("no schema returned")
	}
	return array.NewTableFromRecords(*schema, records), nil
}
