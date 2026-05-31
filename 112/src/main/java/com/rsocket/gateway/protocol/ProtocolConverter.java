package com.rsocket.gateway.protocol;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Component;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Mono;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

@Slf4j
@Component
@RequiredArgsConstructor
public class ProtocolConverter {

    private final Map<String, GrpcServiceDescriptor> grpcServices = new ConcurrentHashMap<>();

    public void registerGrpcService(GrpcServiceDescriptor descriptor) {
        grpcServices.put(descriptor.getServiceName(), descriptor);
        log.info("Registered gRPC service: {}", descriptor.getServiceName());
    }

    public Mono<Object> convertAndCallGrpc(String serviceName, String methodName,
                                           Object payload, Map<String, Object> metadata) {
        GrpcServiceDescriptor service = grpcServices.get(serviceName);
        if (service == null) {
            return Mono.error(new IllegalArgumentException("gRPC service not found: " + serviceName));
        }

        GrpcMethodDescriptor method = service.getMethods().get(methodName);
        if (method == null) {
            return Mono.error(new IllegalArgumentException(
                    "gRPC method not found: " + serviceName + "." + methodName));
        }

        GrpcRequest grpcRequest = GrpcRequest.builder()
                .serviceName(serviceName)
                .methodName(methodName)
                .payload(payload)
                .metadata(metadata)
                .requestType(method.getRequestType())
                .responseType(method.getResponseType())
                .build();

        log.debug("Converting RSocket to gRPC: {}.{}", serviceName, methodName);
        return executeGrpcCall(service, method, grpcRequest);
    }

    public Flux<Object> convertAndCallGrpcStream(String serviceName, String methodName,
                                                  Object payload, Map<String, Object> metadata) {
        GrpcServiceDescriptor service = grpcServices.get(serviceName);
        if (service == null) {
            return Flux.error(new IllegalArgumentException("gRPC service not found: " + serviceName));
        }

        GrpcMethodDescriptor method = service.getMethods().get(methodName);
        if (method == null) {
            return Flux.error(new IllegalArgumentException(
                    "gRPC method not found: " + serviceName + "." + methodName));
        }

        GrpcRequest grpcRequest = GrpcRequest.builder()
                .serviceName(serviceName)
                .methodName(methodName)
                .payload(payload)
                .metadata(metadata)
                .requestType(method.getRequestType())
                .responseType(method.getResponseType())
                .build();

        log.debug("Converting RSocket to gRPC stream: {}.{}", serviceName, methodName);
        return executeGrpcStreamCall(service, method, grpcRequest);
    }

    private Mono<Object> executeGrpcCall(GrpcServiceDescriptor service,
                                          GrpcMethodDescriptor method,
                                          GrpcRequest request) {
        return Mono.fromCallable(() -> {
            log.debug("Executing gRPC unary call: {}.{}", service.getServiceName(), method.getMethodName());
            
            Object convertedPayload = convertPayload(request.getPayload(), request.getRequestType());
            
            Object grpcResponse = callGrpcService(service, method, convertedPayload);
            
            return convertResponse(grpcResponse, request.getResponseType());
        });
    }

    private Flux<Object> executeGrpcStreamCall(GrpcServiceDescriptor service,
                                                GrpcMethodDescriptor method,
                                                GrpcRequest request) {
        return Flux.create(sink -> {
            log.debug("Executing gRPC stream call: {}.{}", service.getServiceName(), method.getMethodName());
            
            try {
                Object convertedPayload = convertPayload(request.getPayload(), request.getRequestType());
                
                callGrpcServiceStream(service, method, convertedPayload,
                        response -> sink.next(convertResponse(response, request.getResponseType())),
                        error -> sink.error(error),
                        () -> sink.complete()
                );
            } catch (Exception e) {
                sink.error(e);
            }
        });
    }

    private Object convertPayload(Object payload, String targetType) {
        if (targetType == null || payload == null) {
            return payload;
        }
        
        log.debug("Converting payload to type: {}", targetType);
        return payload;
    }

    private Object convertResponse(Object response, String responseType) {
        if (responseType == null || response == null) {
            return response;
        }
        
        log.debug("Converting response from type: {}", responseType);
        return response;
    }

    private Object callGrpcService(GrpcServiceDescriptor service,
                                   GrpcMethodDescriptor method,
                                   Object payload) {
        log.info("Calling gRPC service: {}.{}", service.getServiceName(), method.getMethodName());
        
        return Map.of(
                "status", "SUCCESS",
                "service", service.getServiceName(),
                "method", method.getMethodName(),
                "request", payload,
                "timestamp", System.currentTimeMillis()
        );
    }

    private void callGrpcServiceStream(GrpcServiceDescriptor service,
                                       GrpcMethodDescriptor method,
                                       Object payload,
                                       java.util.function.Consumer<Object> onNext,
                                       java.util.function.Consumer<Throwable> onError,
                                       Runnable onComplete) {
        log.info("Calling gRPC stream service: {}.{}", service.getServiceName(), method.getMethodName());
        
        new Thread(() -> {
            try {
                for (int i = 0; i < 5; i++) {
                    onNext.accept(Map.of(
                            "status", "SUCCESS",
                            "service", service.getServiceName(),
                            "method", method.getMethodName(),
                            "sequence", i,
                            "timestamp", System.currentTimeMillis()
                    ));
                    Thread.sleep(1000);
                }
                onComplete.run();
            } catch (Exception e) {
                onError.accept(e);
            }
        }).start();
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class GrpcServiceDescriptor {
        private String serviceName;
        private String host;
        private int port;
        private Map<String, GrpcMethodDescriptor> methods;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class GrpcMethodDescriptor {
        private String methodName;
        private String requestType;
        private String responseType;
        private boolean clientStreaming;
        private boolean serverStreaming;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class GrpcRequest {
        private String serviceName;
        private String methodName;
        private Object payload;
        private Map<String, Object> metadata;
        private String requestType;
        private String responseType;
    }
}
