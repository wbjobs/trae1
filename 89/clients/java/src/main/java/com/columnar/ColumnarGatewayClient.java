package com.columnar;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.stream.StreamSupport;

import org.apache.arrow.memory.BufferAllocator;
import org.apache.arrow.memory.RootAllocator;
import org.apache.arrow.vector.VectorSchemaRoot;
import org.apache.arrow.vector.ipc.ArrowFileReader;
import org.apache.arrow.vector.ipc.ArrowStreamReader;
import org.apache.arrow.vector.ipc.ArrowStreamWriter;
import org.apache.arrow.vector.ipc.ArrowFileWriter;
import org.apache.arrow.vector.ipc.SeekableInMemoryByteChannel;
import org.apache.arrow.vector.types.pojo.Schema;
import org.apache.arrow.vector.util.ByteArrayReadableSeekableByteChannel;
import org.apache.arrow.vector.util.ByteArrayReadableByteChannel;

import com.columnar.proto.ColumnarGatewayGrpc;
import com.columnar.proto.Gateway.*;

import io.grpc.ManagedChannel;
import io.grpc.netty.shaded.io.grpc.netty.NettyChannelBuilder;
import io.grpc.stub.StreamObserver;

/**
 * Arrow Columnar Gateway — Java 客户端。
 *
 * 传输采用 Arrow IPC 格式（上传用 File，下载/查询用 Stream），
 * Arrow 内存块与 gRPC 之间的拷贝仅发生在 Java 层，列式数据保持列式，
 * 因此与 Python 发送的数据等价于同一列式内存视图。
 *
 * <pre>{@code
 * ColumnarGatewayClient client = new ColumnarGatewayClient("localhost", 50051);
 * try (VectorSchemaRoot root = VectorSchemaRoot.create(schema, allocator)) {
 *     // fill root ...
 *     client.upload("demo", root);
 * }
 * VectorSchemaRoot downloaded = client.download("demo", 0);
 * VectorSchemaRoot result     = client.query("demo", "SELECT col1, col2 FROM t WHERE col3 > 10", 0);
 * client.shutdown();
 * }</pre>
 */
public class ColumnarGatewayClient implements AutoCloseable {

    private final ManagedChannel channel;
    private final ColumnarGatewayGrpc.ColumnarGatewayStub stub;
    private final ColumnarGatewayGrpc.ColumnarGatewayBlockingStub blockingStub;
    private final BufferAllocator allocator = new RootAllocator();

    public ColumnarGatewayClient(String host, int port) {
        this.channel = NettyChannelBuilder.forAddress(host, port)
                .maxInboundMessageSize(Integer.MAX_VALUE)
                .usePlaintext()
                .build();
        this.stub = ColumnarGatewayGrpc.newStub(channel);
        this.blockingStub = ColumnarGatewayGrpc.newBlockingStub(channel);
    }

    public BufferAllocator allocator() {
        return allocator;
    }

    // --------------------------------------------------------------- upload

    /** 上传一个已经填充好的 VectorSchemaRoot。 */
    public DatasetInfo upload(String datasetName, VectorSchemaRoot root) throws IOException, InterruptedException {
        // 1. 用 Arrow IPC File 格式将 VectorSchemaRoot（按 batch slice）编码为字节。
        //    服务端 FileReader 可随机访问、多次读取。
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        try (ArrowFileWriter writer = new ArrowFileWriter(root, null, new SeekableInMemoryByteChannel(baos))) {
            // 把 root 的 rows 写成一个 batch
            if (root.getRowCount() > 0) {
                writer.writeBatch();
            }
            writer.end();
        }
        byte[] ipcBytes = baos.toByteArray();

        // 2. 分块发送；gRPC 单消息限制 ~2GB，我们按 64MB 切片。
        final int chunk = 64 * 1024 * 1024;
        final int n = (ipcBytes.length + chunk - 1) / chunk;

        final CompletableStreamObserver<UploadResponse> response = new CompletableStreamObserver<>();
        StreamObserver<UploadRequest> req = stub.upload(response);

        for (int i = 0; i < n; i++) {
            int start = i * chunk;
            int end = Math.min(ipcBytes.length, start + chunk);
            byte[] slice = new byte[end - start];
            System.arraycopy(ipcBytes, start, slice, 0, slice.length);
            req.onNext(UploadRequest.newBuilder()
                    .setDatasetName(i == 0 ? datasetName : "")
                    .setArrowIpc(com.google.protobuf.ByteString.copyFrom(slice))
                    .build());
        }
        req.onCompleted();

        UploadResponse resp = response.get(60, TimeUnit.SECONDS);
        return resp.getInfo();
    }

    // --------------------------------------------------------------- download

    public VectorSchemaRoot download(String datasetName, long version) throws IOException {
        DownloadRequest req = DownloadRequest.newBuilder()
                .setDatasetName(datasetName).setVersion(version).build();
        return readStreamToRoot(blockingStub.download(req));
    }

    // --------------------------------------------------------------- query

    public VectorSchemaRoot query(String datasetName, String sql, long version) throws IOException {
        QueryRequest req = QueryRequest.newBuilder()
                .setDatasetName(datasetName).setSql(sql).setVersion(version).build();
        return readStreamToRoot(blockingStub.query(req));
    }

    private VectorSchemaRoot readStreamToRoot(Iterator<? extends Object> stream) throws IOException {
        // 每个消息是一个 Arrow IPC Stream 片段，每个片段含一个 batch。
        VectorSchemaRoot root = null;
        List<byte[]> cached = new ArrayList<>();
        while (stream.hasNext()) {
            Object msg = stream.next();
            byte[] ipc;
            if (msg instanceof DownloadResponse) {
                ipc = ((DownloadResponse) msg).getArrowIpc().toByteArray();
            } else if (msg instanceof QueryResponse) {
                ipc = ((QueryResponse) msg).getArrowIpc().toByteArray();
            } else {
                throw new IllegalArgumentException("unexpected message type: " + msg.getClass());
            }
            if (ipc.length == 0) continue;
            cached.add(ipc);
        }
        // 重新拼接为一个完整的 Stream（schema 只在首片出现）
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        for (byte[] b : cached) baos.write(b);
        byte[] all = baos.toByteArray();

        try (ArrowStreamReader reader = new ArrowStreamReader(
                new ByteArrayReadableByteChannel(new ByteArrayInputStream(all)), allocator)) {
            root = reader.getVectorSchemaRoot();
            reader.loadNextBatch();
            return root;
        }
    }

    // --------------------------------------------------------------- meta

    public List<String> listDatasets() {
        ListDatasetsResponse resp = blockingStub.listDatasets(ListDatasetsRequest.getDefaultInstance());
        return resp.getNamesList();
    }

    public List<DatasetInfo> listVersions(String datasetName) {
        VersionList resp = blockingStub.listVersions(
                ListVersionsRequest.newBuilder().setDatasetName(datasetName).build());
        return resp.getVersionsList();
    }

    public boolean delete(String datasetName, long version) {
        DeleteResponse resp = blockingStub.delete(
                DeleteRequest.newBuilder().setDatasetName(datasetName).setVersion(version).build());
        return resp.getOk();
    }

    // --------------------------------------------------------------- lifecycle

    public void shutdown() {
        channel.shutdown();
        try {
            if (!channel.awaitTermination(5, TimeUnit.SECONDS)) channel.shutdownNow();
        } catch (InterruptedException ignored) {
            channel.shutdownNow();
        }
    }

    @Override
    public void close() {
        shutdown();
    }

    // ---- helpers ----

    private static class CompletableStreamObserver<T> implements StreamObserver<T> {
        private final java.util.concurrent.CompletableFuture<T> fut = new java.util.concurrent.CompletableFuture<>();

        @Override public void onNext(T value) { fut.complete(value); }
        @Override public void onError(Throwable t) { fut.completeExceptionally(t); }
        @Override public void onCompleted() { if (!fut.isDone()) fut.complete(null); }

        public T get(long timeout, TimeUnit unit) throws java.util.concurrent.TimeoutException,
                java.util.concurrent.ExecutionException, InterruptedException {
            return fut.get(timeout, unit);
        }
    }
}
