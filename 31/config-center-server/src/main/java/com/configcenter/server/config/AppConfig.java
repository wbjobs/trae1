package com.configcenter.server.config;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import io.etcd.jetcd.Client;
import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Data
@Configuration
@ConfigurationProperties(prefix = "config-center")
public class AppConfig {

    private EtcdConfig etcd = new EtcdConfig();
    private GrpcConfig grpc = new GrpcConfig();

    @Data
    public static class EtcdConfig {
        private String endpoints = "http://localhost:2379";
        private String prefix = "/config-center";
    }

    @Data
    public static class GrpcConfig {
        private String host = "0.0.0.0";
        private int port = 9090;
    }

    @Bean
    public ObjectMapper objectMapper() {
        ObjectMapper mapper = new ObjectMapper();
        mapper.registerModule(new JavaTimeModule());
        mapper.disable(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS);
        return mapper;
    }

    @Bean(destroyMethod = "close")
    public Client etcdClient() {
        String[] endpoints = etcd.getEndpoints().split(",");
        return Client.builder().endpoints(endpoints).build();
    }
}
