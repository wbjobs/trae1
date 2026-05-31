module columnar.gateway.client {
    requires io.grpc;
    requires io.grpc.stub;
    requires io.grpc.protobuf;
    requires com.google.protobuf;
    requires org.apache.arrow.vector;
    requires org.apache.arrow.memory;

    exports com.columnar;
}
