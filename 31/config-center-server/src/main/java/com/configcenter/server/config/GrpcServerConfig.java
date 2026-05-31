package com.configcenter.server.config;

import com.configcenter.server.gray.GrayscaleManager;
import com.configcenter.server.grpc.ConfigGrpcService;
import com.configcenter.server.service.ConfigService;
import com.configcenter.server.store.GrayscaleRepository;
import com.configcenter.server.watch.WatchManager;
import io.grpc.Server;
import io.grpc.ServerBuilder;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import lombok.extern.slf4j.Slf4j;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Slf4j
@Configuration
public class GrpcServerConfig {

    @Bean
    public ConfigGrpcService configGrpcService(ConfigService service, WatchManager watchManager,
                                            GrayscaleRepository grayRepo, GrayscaleManager grayManager) {
        return new ConfigGrpcService(service, watchManager, grayRepo, grayManager);
    }

    @Bean
    public GrpcServerLifecycle grpcServerLifecycle(AppConfig config, ConfigGrpcService grpcService) {
        return new GrpcServerLifecycle(config, grpcService);
    }

    @Slf4j
    public static class GrpcServerLifecycle {
        private final AppConfig config;
        private final ConfigGrpcService grpcService;
        private Server server;

        public GrpcServerLifecycle(AppConfig config, ConfigGrpcService grpcService) {
            this.config = config;
            this.grpcService = grpcService;
        }

        @PostConstruct
        public void start() {
            try {
                server = ServerBuilder.forPort(config.getGrpc().getPort())
                        .addService(grpcService)
                        .build()
                        .start();
                log.info("gRPC server listening on {}:{}",
                        config.getGrpc().getHost(), config.getGrpc().getPort());

                Thread t = new Thread(() -> {
                    try {
                        server.awaitTermination();
                    } catch (InterruptedException ignored) {
                        Thread.currentThread().interrupt();
                    }
                });
                t.setDaemon(true);
                t.setName("grpc-server-await");
                t.start();
            } catch (Exception e) {
                throw new IllegalStateException("failed to start gRPC server", e);
            }
        }

        @PreDestroy
        public void stop() {
            if (server != null && !server.isShutdown()) {
                server.shutdown();
                log.info("gRPC server shutdown");
            }
        }
    }
}
