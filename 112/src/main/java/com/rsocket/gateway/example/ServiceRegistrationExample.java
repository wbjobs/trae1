package com.rsocket.gateway.example;

import com.rsocket.gateway.handler.ServiceRegistrationHandler;
import lombok.extern.slf4j.Slf4j;
import org.springframework.messaging.rsocket.RSocketRequester;
import org.springframework.stereotype.Component;
import reactor.core.publisher.Mono;

import java.util.Arrays;
import java.util.UUID;

@Slf4j
@Component
public class ServiceRegistrationExample {

    private final RSocketRequester.Builder requesterBuilder;

    public ServiceRegistrationExample(RSocketRequester.Builder requesterBuilder) {
        this.requesterBuilder = requesterBuilder;
    }

    public Mono<Void> registerExampleService() {
        return requesterBuilder
                .tcp("localhost", 7000)
                .flatMap(requester -> {
                    ServiceRegistrationHandler.RegistrationRequest request =
                            ServiceRegistrationHandler.RegistrationRequest.builder()
                                    .serviceName("userService")
                                    .instanceId(UUID.randomUUID().toString())
                                    .host("localhost")
                                    .port(7100)
                                    .weight(10)
                                    .methods(Arrays.asList("getUser", "createUser", "updateUser", "deleteUser"))
                                    .build();

                    return requester.route("register-service")
                            .data(request)
                            .retrieveMono(ServiceRegistrationHandler.RegistrationResponse.class)
                            .doOnSuccess(response -> log.info("Service registration response: {}", response))
                            .doOnError(error -> log.error("Failed to register service", error))
                            .then();
                });
    }
}
