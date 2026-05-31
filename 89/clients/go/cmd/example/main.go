// Example: 使用 Go 客户端与网关交互。
//
//   1. 启动网关（Rust 端）
//   2. go run ./cmd/example
package main

import (
	"context"
	"fmt"
	"log"
	"time"

	"github.com/apache/arrow/go/v18/arrow"
	"github.com/apache/arrow/go/v18/arrow/array"

	columnar "github.com/columnar-gateway/go-client"
)

func main() {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	client, err := columnar.New(ctx, "localhost:50051")
	if err != nil {
		log.Fatal(err)
	}
	defer client.Close()

	// 构造一个示例 Table: [{a: 1, b: 1.1}, {a: 2, b: 2.2}, {a: 3, b: 3.3}]
	schema := arrow.NewSchema([]arrow.Field{
		{Name: "a", Type: arrow.PrimitiveTypes.Int64, Nullable: true},
		{Name: "b", Type: arrow.PrimitiveTypes.Float64, Nullable: true},
	}, nil)

	b := array.NewRecordBuilder(nil, schema)
	defer b.Release()
	b.Field(0).(*array.Int64Builder).AppendValues([]int64{1, 2, 3}, nil)
	b.Field(1).(*array.Float64Builder).AppendValues([]float64{1.1, 2.2, 3.3}, nil)
	rec := b.NewRecord()
	defer rec.Release()
	tbl := array.NewTableFromRecords(schema, []arrow.Record{rec})
	defer tbl.Release()

	if _, err := client.Upload(ctx, "demo", tbl); err != nil {
		log.Fatalf("upload: %v", err)
	}

	// 查询
	qtbl, err := client.Query(ctx, "demo", "SELECT a, b FROM t WHERE a > 1", 0)
	if err != nil {
		log.Fatalf("query: %v", err)
	}
	defer qtbl.Release()
	fmt.Println("query result rows:", qtbl.NumRows())

	// 列出
	names, _ := client.ListDatasets(ctx)
	fmt.Println("datasets:", names)
}
