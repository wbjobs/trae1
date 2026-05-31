package com.rsocket.gateway.handler;

import com.rsocket.gateway.stream.ServerPushManager;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.messaging.handler.annotation.MessageMapping;
import org.springframework.messaging.handler.annotation.Payload;
import org.springframework.stereotype.Controller;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Mono;

@Slf4j
@Controller
@RequiredArgsConstructor
public class PushHandler {

    private final ServerPushManager serverPushManager;

    @MessageMapping("subscribe-push")
    public Flux<Object> subscribePush(@Payload String clientId) {
        log.info("Client {} subscribed to push notifications", clientId);
        return serverPushManager.subscribeToPush(clientId)
                .doFinally(signalType -> {
                    log.info("Client {} push subscription ended", clientId);
                    serverPushManager.removeClient(clientId);
                });
    }

    @MessageMapping("send-push")
    public Mono<Void> sendPush(@Payload PushMessage message) {
        log.info("Sending push to client: {}", message.getClientId());
        if (message.getClientId() != null) {
            serverPushManager.pushToClient(message.getClientId(), message.getPayload());
        } else {
            serverPushManager.broadcast(message.getPayload());
        }
        return Mono.empty();
    }

    @lombok.Data
    @lombok.AllArgsConstructor
    @lombok.NoArgsConstructor
    @lombok.Builder
    public static class PushMessage {
        private String clientId;
        private Object payload;
    }
}
