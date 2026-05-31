package com.dql.mq.config;

import com.dql.mq.entity.QueueConfig;
import com.dql.mq.repository.QueueConfigRepository;
import jakarta.annotation.PostConstruct;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.amqp.core.*;
import org.springframework.amqp.rabbit.core.RabbitAdmin;
import org.springframework.context.annotation.Configuration;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

@Slf4j
@Configuration
@RequiredArgsConstructor
public class QueueBindingConfig {

    private final RabbitAdmin rabbitAdmin;
    private final QueueConfigRepository queueConfigRepository;

    @PostConstruct
    public void initQueues() {
        List<QueueConfig> configs = queueConfigRepository.findByEnabledTrueAndAutoDlqTrue();
        for (QueueConfig config : configs) {
            try {
                declareQueueAndBindings(config);
            } catch (Exception e) {
                log.error("初始化队列失败: {}", config.getQueueName(), e);
            }
        }
        log.info("初始化队列完成，共初始化 {} 个队列", configs.size());
    }

    public void declareQueueAndBindings(QueueConfig config) {
        Map<String, Object> args = new HashMap<>();

        if (config.getDlqExchangeName() != null && !config.getDlqExchangeName().isEmpty()) {
            args.put("x-dead-letter-exchange", config.getDlqExchangeName());
            if (config.getDlqRoutingKey() != null && !config.getDlqRoutingKey().isEmpty()) {
                args.put("x-dead-letter-routing-key", config.getDlqRoutingKey());
            }
        }

        Queue queue = QueueBuilder.durable(config.getQueueName())
                .withArguments(args)
                .build();

        Exchange exchange = createExchange(config.getExchangeName());
        Binding binding = createBinding(queue, exchange, config.getRoutingKey());

        rabbitAdmin.declareQueue(queue);
        rabbitAdmin.declareExchange(exchange);
        rabbitAdmin.declareBinding(binding);

        if (config.getDlqExchangeName() != null && !config.getDlqExchangeName().isEmpty()) {
            String dlqQueueName = config.getDlqQueueName() != null && !config.getDlqQueueName().isEmpty()
                    ? config.getDlqQueueName()
                    : config.getQueueName() + ".dlq";

            Queue dlqQueue = QueueBuilder.durable(dlqQueueName).build();
            Exchange dlqExchange = createExchange(config.getDlqExchangeName());

            String dlqRoutingKey = config.getDlqRoutingKey() != null && !config.getDlqRoutingKey().isEmpty()
                    ? config.getDlqRoutingKey()
                    : config.getRoutingKey() + ".dlq";

            Binding dlqBinding = createBinding(dlqQueue, dlqExchange, dlqRoutingKey);

            rabbitAdmin.declareQueue(dlqQueue);
            rabbitAdmin.declareExchange(dlqExchange);
            rabbitAdmin.declareBinding(dlqBinding);

            log.info("声明死信队列: {}, 死信交换机: {}, 路由键: {}",
                    dlqQueueName, config.getDlqExchangeName(), dlqRoutingKey);
        }

        log.info("声明队列: {}, 交换机: {}, 路由键: {}",
                config.getQueueName(), config.getExchangeName(), config.getRoutingKey());
    }

    private Exchange createExchange(String exchangeName) {
        if (exchangeName.endsWith(".fanout")) {
            return ExchangeBuilder.fanoutExchange(exchangeName)
                    .durable(true)
                    .build();
        } else if (exchangeName.endsWith(".topic")) {
            return ExchangeBuilder.topicExchange(exchangeName)
                    .durable(true)
                    .build();
        } else {
            return ExchangeBuilder.directExchange(exchangeName)
                    .durable(true)
                    .build();
        }
    }

    private Binding createBinding(Queue queue, Exchange exchange, String routingKey) {
        if (exchange instanceof FanoutExchange) {
            return BindingBuilder.bind(queue).to((FanoutExchange) exchange);
        } else if (exchange instanceof TopicExchange) {
            return BindingBuilder.bind(queue).to((TopicExchange) exchange).with(routingKey);
        } else {
            return BindingBuilder.bind(queue).to((DirectExchange) exchange).with(routingKey);
        }
    }

    public void removeQueueAndBindings(QueueConfig config) {
        try {
            rabbitAdmin.deleteQueue(config.getQueueName());
            if (config.getDlqQueueName() != null && !config.getDlqQueueName().isEmpty()) {
                rabbitAdmin.deleteQueue(config.getDlqQueueName());
            }
            log.info("删除队列: {}", config.getQueueName());
        } catch (Exception e) {
            log.error("删除队列失败: {}", config.getQueueName(), e);
            throw new RuntimeException("删除队列失败", e);
        }
    }
}
