#!/usr/bin/env python3
"""Arrow Columnar Gateway — Python client.

支持 pandas DataFrame 与 Arrow Table 双向传输，
传输过程中直接使用 Arrow IPC 字节，避免二次序列化。

用法：
    from columnar_client import ColumnarGatewayClient

    client = ColumnarGatewayClient("http://localhost:50051")
    import pandas as pd
    df = pd.DataFrame({"a": [1, 2, 3], "b": [1.1, 2.2, 3.3]})
    result = client.upload_dataframe("demo", df)
    print(result.info)
    if result.optimization:
        print(f"Saved {result.optimization.total_saving_ratio:.0%} memory!")

    df2 = client.download_dataframe("demo")
    print(df2)

    result_table = client.query("demo", "SELECT a, b FROM t WHERE a > 1")
    print(result_table)

    print(client.list_datasets())
    print(client.list_versions("demo"))
"""

from __future__ import annotations

import io
import json
import os
from dataclasses import dataclass, field
from typing import Iterable, List, Optional

import grpc
import pyarrow as pa
import pyarrow.ipc as ipc

from . import gateway_pb2, gateway_pb2_grpc


@dataclass
class DatasetInfo:
    name: str
    version: int
    created_at: int
    row_count: int
    bytes: int
    schema_json: str


@dataclass
class ColumnOptimization:
    column_name: str
    original_type: str
    optimized_type: str
    encoding: str
    memory_saving_ratio: float
    original_bytes: int
    optimized_bytes: int


@dataclass
class SchemaOptimization:
    columns: List[ColumnOptimization] = field(default_factory=list)
    compression: str = ""
    total_original_bytes: int = 0
    total_optimized_bytes: int = 0
    total_saving_ratio: float = 0.0


@dataclass
class UploadResult:
    info: DatasetInfo
    optimization: Optional[SchemaOptimization] = None


def _proto_to_dataset_info(info: gateway_pb2.DatasetInfo) -> DatasetInfo:
    return DatasetInfo(
        name=info.name,
        version=info.version,
        created_at=info.created_at,
        row_count=info.row_count,
        bytes=info.bytes,
        schema_json=info.schema_json,
    )


def _proto_to_optimization(opt: gateway_pb2.SchemaOptimization) -> SchemaOptimization:
    cols = [
        ColumnOptimization(
            column_name=c.column_name,
            original_type=c.original_type,
            optimized_type=c.optimized_type,
            encoding=c.encoding,
            memory_saving_ratio=c.memory_saving_ratio,
            original_bytes=c.original_bytes,
            optimized_bytes=c.optimized_bytes,
        )
        for c in opt.columns
    ]
    return SchemaOptimization(
        columns=cols,
        compression=opt.compression,
        total_original_bytes=opt.total_original_bytes,
        total_optimized_bytes=opt.total_optimized_bytes,
        total_saving_ratio=opt.total_saving_ratio,
    )


class ColumnarGatewayClient:
    def __init__(self, target: str, *, options: Optional[List[tuple]] = None):
        """target: 'http://host:port' or 'host:port'."""
        opts = options or [
            ("grpc.max_send_message_length", 2 * 1024 * 1024 * 1024),
            ("grpc.max_receive_message_length", 2 * 1024 * 1024 * 1024),
            ("grpc.max_metadata_size", 64 * 1024 * 1024),
        ]
        self.channel = grpc.insecure_channel(target, options=opts)
        self.stub = gateway_pb2_grpc.ColumnarGatewayStub(self.channel)

    # ------------------------------------------------------------------ upload

    def _iter_record_batches(self, table: pa.Table, batch_rows: int = 65536):
        for batch in table.to_batches(max_chunksize=batch_rows):
            buf = io.BytesIO()
            with ipc.new_file(buf, schema=table.schema) as writer:
                writer.write_batch(batch)
            yield buf.getvalue()

    def upload_table(
        self,
        dataset_name: str,
        table: pa.Table,
        *,
        batch_rows: int = 65536,
    ) -> UploadResult:
        """Upload a pyarrow Table as a new dataset version.

        Returns UploadResult (info + optional optimization report).
        """

        def gen():
            # 首条消息携带 dataset_name + schema 片
            first = True
            for ipc_bytes in self._iter_record_batches(table, batch_rows):
                yield gateway_pb2.UploadRequest(
                    dataset_name=dataset_name if first else "",
                    arrow_ipc=ipc_bytes,
                )
                first = False
            # 保证至少有一条；空表则只发消息
            if first:
                yield gateway_pb2.UploadRequest(dataset_name=dataset_name, arrow_ipc=b"")

        resp = self.stub.Upload(gen())
        info = _proto_to_dataset_info(resp.info)
        opt = None
        # Check if optimization field is populated (HasField for proto3 optional)
        if resp.HasField("optimization"):
            opt = _proto_to_optimization(resp.optimization)
        return UploadResult(info=info, optimization=opt)

    def upload_dataframe(
        self,
        dataset_name: str,
        df,
        *,
        batch_rows: int = 65536,
    ) -> UploadResult:
        """Upload a pandas DataFrame.

        pandas -> Arrow 是零拷贝（若底层数据块对齐），
        然后通过 Arrow IPC File 格式发送给网关。
        返回 UploadResult（包含 info + optimization 优化报告）。
        """
        table = pa.Table.from_pandas(df)
        return self.upload_table(dataset_name, table, batch_rows=batch_rows)

    # ------------------------------------------------------------------ download

    def download_table(
        self, dataset_name: str, *, version: int = 0
    ) -> pa.Table:
        req = gateway_pb2.DownloadRequest(dataset_name=dataset_name, version=version)
        stream = self.stub.Download(req)
        batches: List[pa.RecordBatch] = []
        schema: Optional[pa.Schema] = None
        for msg in stream:
            if not msg.arrow_ipc:
                continue
            reader = ipc.open_stream(msg.arrow_ipc)
            if schema is None:
                schema = reader.schema
            for b in reader:
                batches.append(b)
        if schema is None:
            return pa.table({})
        return pa.Table.from_batches(batches, schema=schema)

    def download_dataframe(self, dataset_name: str, *, version: int = 0):
        table = self.download_table(dataset_name, version=version)
        return table.to_pandas()

    # ------------------------------------------------------------------ query

    def query(
        self,
        dataset_name: str,
        sql: str,
        *,
        version: int = 0,
    ) -> pa.Table:
        """Run a SQL query on the gateway. Returns a pyarrow Table.

        示例:
            client.query("demo", "SELECT col1, col2 FROM t WHERE col3 > 10")
            client.query("demo", "SELECT col1, col2 WHERE col3 > 10")  # FROM t 自动补全
        """
        req = gateway_pb2.QueryRequest(
            dataset_name=dataset_name, sql=sql, version=version
        )
        stream = self.stub.Query(req)
        batches: List[pa.RecordBatch] = []
        schema: Optional[pa.Schema] = None
        for msg in stream:
            if not msg.arrow_ipc:
                continue
            reader = ipc.open_stream(msg.arrow_ipc)
            if schema is None:
                schema = reader.schema
            for b in reader:
                batches.append(b)
        if schema is None:
            return pa.table({})
        return pa.Table.from_batches(batches, schema=schema)

    def query_as_dataframe(self, dataset_name: str, sql: str, *, version: int = 0):
        return self.query(dataset_name, sql, version=version).to_pandas()

    # ------------------------------------------------------------------ meta

    def list_datasets(self) -> List[str]:
        resp = self.stub.ListDatasets(gateway_pb2.ListDatasetsRequest())
        return list(resp.names)

    def list_versions(self, dataset_name: str) -> List[DatasetInfo]:
        resp = self.stub.ListVersions(
            gateway_pb2.ListVersionsRequest(dataset_name=dataset_name)
        )
        return [
            DatasetInfo(
                name=v.name,
                version=v.version,
                created_at=v.created_at,
                row_count=v.row_count,
                bytes=v.bytes,
                schema_json=v.schema_json,
            )
            for v in resp.versions
        ]

    def delete(self, dataset_name: str, *, version: int = 0) -> bool:
        resp = self.stub.Delete(
            gateway_pb2.DeleteRequest(dataset_name=dataset_name, version=version)
        )
        return resp.ok

    # ------------------------------------------------------------------ 联合查询：数据源管理

    def register_datasource(
        self,
        name: str,
        datasource_type: str,
        connection_string: str,
        options: Optional[dict] = None,
    ) -> tuple[bool, str]:
        """注册数据源

        Args:
            name: 数据源名称（SQL中作为 catalog 使用）
            datasource_type: "postgres" / "mysql" / "s3_parquet" / "local_parquet"
            connection_string: 连接字符串
            options: 额外配置（如 region, access_key, path 等）
        """
        type_map = {
            "postgres": gateway_pb2.DATASOURCE_POSTGRES,
            "mysql": gateway_pb2.DATASOURCE_MYSQL,
            "s3_parquet": gateway_pb2.DATASOURCE_S3_PARQUET,
            "local_parquet": gateway_pb2.DATASOURCE_LOCAL_PARQUET,
        }
        ds_type = type_map.get(datasource_type.lower(), gateway_pb2.DATASOURCE_UNKNOWN)

        config = gateway_pb2.DatasourceConfig(
            name=name,
            type=ds_type,
            connection_string=connection_string,
            options=options or {},
        )
        resp = self.stub.RegisterDatasource(
            gateway_pb2.RegisterDatasourceRequest(config=config)
        )
        return resp.ok, resp.message

    def drop_datasource(self, name: str) -> bool:
        resp = self.stub.DropDatasource(gateway_pb2.DropDatasourceRequest(name=name))
        return resp.ok

    def list_datasources(self) -> list:
        resp = self.stub.ListDatasources(gateway_pb2.ListDatasourcesRequest())
        return [
            {
                "name": ds.name,
                "type": ds.type,
                "connection_string": ds.connection_string,
                "options": dict(ds.options),
            }
            for ds in resp.datasources
        ]

    # ------------------------------------------------------------------ 联合查询：查询执行

    @dataclass
    class FederatedQueryResult:
        table: pa.Table
        execution_plan: Optional[dict]
        elapsed_ms: int
        accessed_datasources: List[str]

    def federated_query(
        self,
        sql: str,
        *,
        timeout_seconds: int = 0,
        include_explain: bool = False,
    ) -> FederatedQueryResult:
        """跨数据源联合查询

        SQL 中表名格式: datasource_name.table_name
        例如: SELECT * FROM pg_db.users JOIN mysql_db.orders ON users.id = orders.user_id
        """
        req = gateway_pb2.FederatedQueryRequest(
            sql=sql,
            timeout_seconds=timeout_seconds,
            include_explain=include_explain,
        )
        stream = self.stub.FederatedQuery(req)

        batches: List[pa.RecordBatch] = []
        schema: Optional[pa.Schema] = None
        execution_plan = None
        elapsed_ms = 0

        for msg in stream:
            if msg.execution_plan_json:
                execution_plan = json.loads(msg.execution_plan_json)
            if msg.elapsed_ms > 0:
                elapsed_ms = msg.elapsed_ms
            if not msg.arrow_ipc:
                continue

            buf = io.BytesIO(msg.arrow_ipc)
            reader = ipc.open_stream(buf)
            if schema is None:
                schema = reader.schema
            for b in reader:
                batches.append(b)

        if schema is None:
            table = pa.table({})
        else:
            table = pa.Table.from_batches(batches, schema=schema)

        return self.FederatedQueryResult(
            table=table,
            execution_plan=execution_plan,
            elapsed_ms=elapsed_ms,
            accessed_datasources=[],
        )

    def federated_query_as_dataframe(
        self,
        sql: str,
        *,
        timeout_seconds: int = 0,
        include_explain: bool = False,
    ):
        result = self.federated_query(
            sql, timeout_seconds=timeout_seconds, include_explain=include_explain
        )
        return result.table.to_pandas(), result.execution_plan, result.elapsed_ms

    # ------------------------------------------------------------------ 联合查询：执行计划

    def explain(self, sql: str, *, verbose: bool = False) -> tuple[str, dict]:
        """获取查询执行计划

        Returns: (plan_text, plan_json_dict)
        """
        resp = self.stub.Explain(gateway_pb2.ExplainRequest(sql=sql, verbose=verbose))
        plan_json = json.loads(resp.plan_json)
        return resp.plan_text, plan_json

    # ------------------------------------------------------------------ 联合查询：慢查询日志

    def slow_query_log(self, *, limit: int = 100) -> List[dict]:
        """获取慢查询日志（>5秒的查询）"""
        resp = self.stub.SlowQueryLog(gateway_pb2.SlowQueryLogRequest(limit=limit))
        return [
            {
                "query_id": q.query_id,
                "sql": q.sql,
                "start_time_ms": q.start_time_ms,
                "elapsed_ms": q.elapsed_ms,
                "rows_returned": q.rows_returned,
                "is_slow": q.is_slow,
                "accessed_datasources": list(q.accessed_datasources),
                "error": q.error,
            }
            for q in resp.queries
        ]

    def close(self):
        self.channel.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


__all__ = [
    "ColumnarGatewayClient",
    "DatasetInfo",
    "UploadResult",
    "SchemaOptimization",
    "ColumnOptimization",
]
