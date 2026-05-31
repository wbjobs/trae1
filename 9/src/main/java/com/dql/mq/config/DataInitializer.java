package com.dql.mq.config;

import com.dql.mq.entity.QueueConfig;
import com.dql.mq.repository.QueueConfigRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.CommandLineRunner;
import org.springframework.stereotype.Component;

import java.time.LocalDateTime;

@Slf4j
@Component
@RequiredArgsConstructor
public class DataInitializer implements CommandLineRunner {

    private final QueueConfigRepository queueConfigRepository;
    private final QueueBindingConfig queueBindingConfig;
    private final DynamicListenerConfig dynamicListenerConfig;

    @Override
    public void run(String... args) {
        log.info("开始初始化默认队列配置...");

        initQueueConfig(
                "order.queue",
                "order.exchange",
                "order.create",
                "order.exchange.dlq",
                "order.create.dlq",
                "order.queue.dlq",
                "订单队列"
        );

        initQueueConfig(
                "payment.queue",
                "payment.exchange",
                "payment.process",
                "payment.exchange.dlq",
                "payment.process.dlq",
                "payment.queue.dlq",
                "支付队列"
        );

        initQueueConfig(
                "notification.queue",
                "notification.exchange",
                "notification.send",
                "notification.exchange.dlq",
                "notification.send.dlq",
                "notification.queue.dlq",
                "通知队列"
        );

        log.info("默认队列配置初始化完成");
    }

    private void initQueueConfig(String queueName, String exchangeName, String routingKey,
                                 String dlqExchangeName, String dlqRoutingKey, String dlqQueueName,
                                 String description) {
        if (queueConfigRepository.findByQueueName(queueName).isPresent()) {
            log.debug("队列配置已存在，跳过初始化: {}", queueName);
            return;
        }

        QueueConfig config = QueueConfig.builder()
                .queueName(queueName)
                .exchangeName(exchangeName)
                .routingKey(routingKey)
                .dlqExchangeName(dlqExchangeName)
                .dlqRoutingKey(dlqRoutingKey)
                .dlqQueueName(dlqQueueName)
                .maxRetryCount(3)
                .retryInterval(5000L)
                .enabled(true)
                .autoDlq(true)
                .acknowledgeMode("MANUAL")
                .prefetchCount(10)
                .concurrency(3)
                .maxConcurrency(10)
                .description(description)
                .createdTime(LocalDateTime.now())
                .build();

        QueueConfig saved = queueConfigRepository.save(config);
        log.info("初始化队列配置: {}", queueName);

        try {
            queueBindingConfig.declareQueueAndBindings(saved);
            if (saved.getEnabled() != null && saved.getEnabled()) {
                dynamicListenerConfig.createAndStartListener(saved);
            }
        } catch (Exception e) {
            log.error("初始化队列绑定或监听器失败: {}", queueName, e);
        }
    }
}
