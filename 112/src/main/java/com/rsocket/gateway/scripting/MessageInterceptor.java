package com.rsocket.gateway.scripting;

import com.rsocket.gateway.scripting.model.InterceptContext;
import com.rsocket.gateway.scripting.model.ScriptExecutionResult;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Mono;

import java.time.Instant;
import java.util.List;
import java.util.Map;

@Slf4j
@Service
@RequiredArgsConstructor
public class MessageInterceptor {

    private final ScriptManager scriptManager;

    public InterceptContext createContext(String interactionType,
                                           String route,
                                           Object payload,
                                           Map<String, Object> metadata,
                                           String clientId,
                                           String connectionId) {
        return InterceptContext.builder()
                .interactionType(interactionType)
                .route(route)
                .payload(payload)
                .metadata(metadata != null ? metadata : new java.util.HashMap<>())
                .clientId(clientId)
                .connectionId(connectionId)
                .timestamp(Instant.now())
                .build();
    }

    public Mono<InterceptContext> interceptBefore(InterceptContext context) {
        return Mono.fromCallable(() -> {
            List<ScriptExecutionResult> results = scriptManager.executeBeforeInterceptors(context);
            
            if (context.isRejected()) {
                log.info("Request rejected by interceptors: {}", context.getRejectionReason());
                throw new SecurityException("Request rejected: " + context.getRejectionReason());
            }
            
            return context;
        });
    }

    public Mono<InterceptContext> interceptAfter(InterceptContext context, Object response) {
        context.setResponse(response);
        return Mono.fromCallable(() -> {
            scriptManager.executeAfterInterceptors(context);
            return context;
        });
    }

    public Mono<InterceptContext> interceptError(InterceptContext context, Throwable error) {
        context.setError(error);
        return Mono.fromCallable(() -> {
            scriptManager.executeErrorInterceptors(context);
            return context;
        });
    }

    public <T> Mono<T> interceptRequestResponse(String route,
                                                 Object payload,
                                                 Map<String, Object> metadata,
                                                 String clientId,
                                                 String connectionId,
                                                 java.util.function.Function<InterceptContext, Mono<T>> handler) {
        InterceptContext context = createContext("request-response", route, payload, metadata, clientId, connectionId);

        return interceptBefore(context)
                .flatMap(ctx -> {
                    Object effectivePayload = ctx.isModified() ? ctx.getPayload() : payload;
                    return handler.apply(ctx)
                            .flatMap(response -> interceptAfter(ctx, response)
                                    .thenReturn((T) (ctx.isModified() ? ctx.getResponse() : response)));
                })
                .onErrorResume(e -> interceptError(context, e)
                        .then(Mono.error(e)));
    }

    public <T> Flux<T> interceptRequestStream(String route,
                                                Object payload,
                                                Map<String, Object> metadata,
                                                String clientId,
                                                String connectionId,
                                                java.util.function.Function<InterceptContext, Flux<T>> handler) {
        InterceptContext context = createContext("request-stream", route, payload, metadata, clientId, connectionId);

        return interceptBefore(context)
                .flatMapMany(ctx -> {
                    Object effectivePayload = ctx.isModified() ? ctx.getPayload() : payload;
                    return handler.apply(ctx)
                            .concatWith(Flux.defer(() ->
                                    Mono.fromRunnable(() -> interceptAfter(ctx, null))
                                            .then(Mono.empty())
                            ));
                })
                .onErrorResume(e -> Flux.defer(() ->
                        interceptError(context, e)
                                .thenMany(Flux.error(e))
                ));
    }

    public Mono<Void> interceptFireAndForget(String route,
                                              Object payload,
                                              Map<String, Object> metadata,
                                              String clientId,
                                              String connectionId,
                                              java.util.function.Function<InterceptContext, Mono<Void>> handler) {
        InterceptContext context = createContext("fire-and-forget", route, payload, metadata, clientId, connectionId);

        return interceptBefore(context)
                .flatMap(ctx -> {
                    Object effectivePayload = ctx.isModified() ? ctx.getPayload() : payload;
                    return handler.apply(ctx)
                            .then(interceptAfter(ctx, null))
                            .then();
                })
                .onErrorResume(e -> interceptError(context, e)
                        .then(Mono.error(e)));
    }

    public <T> Flux<T> interceptChannel(String route,
                                         Flux<Object> payloads,
                                         Map<String, Object> metadata,
                                         String clientId,
                                         String connectionId,
                                         java.util.function.Function<InterceptContext, Flux<T>> handler) {
        return payloads.concatMap(payload -> {
            InterceptContext context = createContext("channel", route, payload, metadata, clientId, connectionId);
            return interceptBefore(context)
                    .flatMapMany(ctx -> {
                        Object effectivePayload = ctx.isModified() ? ctx.getPayload() : payload;
                        return handler.apply(ctx)
                                .concatWith(Flux.defer(() ->
                                        Mono.fromRunnable(() -> interceptAfter(ctx, null))
                                                .then(Mono.empty())
                                ));
                    })
                    .onErrorResume(e -> Flux.defer(() ->
                            interceptError(context, e)
                                    .thenMany(Flux.error(e))
                    ));
        });
    }
}
