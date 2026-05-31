package com.rsocket.gateway.controller;

import com.rsocket.gateway.protocol.ProtocolConverter;
import com.rsocket.gateway.scripting.ScriptManager;
import com.rsocket.gateway.scripting.model.ScriptDefinition;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;
import reactor.core.publisher.Mono;

import java.util.List;

@RestController
@RequestMapping("/api/protocol")
@RequiredArgsConstructor
public class ProtocolController {

    private final ProtocolConverter protocolConverter;
    private final ScriptManager scriptManager;

    @PostMapping("/grpc/register")
    public Mono<Void> registerGrpcService(@RequestBody ProtocolConverter.GrpcServiceDescriptor descriptor) {
        protocolConverter.registerGrpcService(descriptor);
        return Mono.empty();
    }

    @PostMapping("/grpc/convert/{serviceName}/{methodName}")
    public Mono<Object> convertAndCallGrpc(
            @PathVariable String serviceName,
            @PathVariable String methodName,
            @RequestBody Object payload) {
        return protocolConverter.convertAndCallGrpc(serviceName, methodName, payload, new java.util.HashMap<>());
    }

    @PostMapping("/grpc/convert-stream/{serviceName}/{methodName}")
    public reactor.core.publisher.Flux<Object> convertAndCallGrpcStream(
            @PathVariable String serviceName,
            @PathVariable String methodName,
            @RequestBody Object payload) {
        return protocolConverter.convertAndCallGrpcStream(serviceName, methodName, payload, new java.util.HashMap<>());
    }

    @PostMapping("/grpc/create-converter-script")
    public Mono<ScriptDefinition> createGrpcConverterScript(
            @RequestParam String scriptName,
            @RequestParam String grpcServiceName,
            @RequestParam String routePattern) {
        String script = """
            if (route?.startsWith("%s")) {
                def grpcMethod = route.replace("%s.", "")
                def grpcService = "%s"
                
                def grpcRequest = [
                    service: grpcService,
                    method: grpcMethod,
                    payload: payload,
                    metadata: metadata
                ]
                
                context.addMetadata("grpc-service", grpcService)
                context.addMetadata("grpc-method", grpcMethod)
                context.addMetadata("protocol", "grpc")
                context.modifyPayload(grpcRequest)
                
                log.debug("Converted RSocket to gRPC: {}.{}", grpcService, grpcMethod)
            }
            """.formatted(routePattern, routePattern, grpcServiceName);

        ScriptDefinition definition = ScriptDefinition.builder()
                .name(scriptName)
                .description("RSocket to gRPC converter for service: " + grpcServiceName)
                .script(script)
                .language("groovy")
                .interceptPoint(ScriptDefinition.InterceptPoint.BEFORE)
                .routes(List.of(routePattern + ".*"))
                .interactionTypes(List.of("*"))
                .priority(300)
                .enabled(true)
                .timeoutMs(50)
                .fallbackStrategy(ScriptDefinition.FallbackStrategy.ALLOW)
                .build();

        return Mono.just(scriptManager.registerScript(definition));
    }
}
