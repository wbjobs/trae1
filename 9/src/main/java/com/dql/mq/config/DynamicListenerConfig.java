package com.dql.mq.config;

import com.dql.mq.entity.QueueConfig;
import com.dql.mq.handler.DeadLetterHandler;
import com.dql.mq.handler.MessageHandler;
import com.dql.mq.repository.QueueConfigRepository;
import jakarta.annotation.PostConstruct;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.amqp.rabbit.listener.SimpleMessageListenerContainer;
import org.springframework.amqp.rabbit.listener.adapter.MessageListenerAdapter;
import org.springframework.context.annotation.Configuration;

import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;

@Slf4j
@Configuration
@RequiredArgsConstructor
public class DynamicListenerConfig {

    private final QueueConfigRepository queueConfigRepository;
    private final org.springframework.amqp.rabbit.connection.ConnectionFactory connectionFactory;
    private final MessageHandler messageHandler;
    private final DeadLetterHandler deadLetterHandler;
    private final QueueBindingConfig queueBindingConfig;

    private final Map<String, SimpleMessageListenerContainer> listenerContainers = new ConcurrentHashMap<>();
    private final Map<String, ReentrantLock> listenerLocks = new ConcurrentHashMap<>();

    @PostConstruct
    public void initListeners() {
        List<QueueConfig> configs = queueConfigRepository.findByEnabledTrue();
        for (QueueConfig config : configs) {
            try {
                queueBindingConfig.declareQueueAndBindings(config);
                createAndStartListener(config);
            } catch (Exception e) {
                log.error("创建消息监听器失败: {}", config.getQueueName(), e);
            }
        }
        log.info("初始化消息监听器完成，共初始化 {} 个队列配置", configs.size());
    }

    public void createAndStartListener(QueueConfig config) {
        createMainQueueListener(config);
        createDlqListener(config);
    }

    private void createMainQueueListener(QueueConfig config) {
        String queueName = config.getQueueName();
        createListener(queueName, queueName, messageHandler, "handleMessage", config);
    }

    private void createDlqListener(QueueConfig config) {
        String dlqQueueName = config.getDlqQueueName();
        if (dlqQueueName == null || dlqQueueName.isEmpty()) {
            dlqQueueName = config.getQueueName() + ".dlq";
        }
        String dlqListenerKey = config.getQueueName() + ".dlq.listener";
        createListener(dlqListenerKey, dlqQueueName, deadLetterHandler, "handleDeadLetter", config);
    }

    private void createListener(String listenerKey, String queueName, Object handler,
                                String methodName, QueueConfig config) {
        ReentrantLock lock = listenerLocks.computeIfAbsent(listenerKey, k -> new ReentrantLock());
        lock.lock();
        try {
            if (listenerContainers.containsKey(listenerKey)) {
                log.warn("监听器已存在，跳过创建: {}", listenerKey);
                return;
            }

            try {
                SimpleMessageListenerContainer container = new SimpleMessageListenerContainer();
                container.setConnectionFactory(connectionFactory);
                container.setQueueNames(queueName);
                container.setAcknowledgeMode(org.springframework.amqp.core.AcknowledgeMode.MANUAL);
                container.setPrefetchCount(config.getPrefetchCount() != null ? config.getPrefetchCount() : 10);
                container.setConcurrentConsumers(config.getConcurrency() != null ? config.getConcurrency() : 3);
                container.setMaxConcurrentConsumers(config.getMaxConcurrency() != null ? config.getMaxConcurrency() : 10);
                container.setAutoStartup(true);

                MessageListenerAdapter adapter = new MessageListenerAdapter(handler);
                adapter.setDefaultListenerMethod(methodName);
                container.setMessageListener(adapter);

                container.initialize();
                container.start();

                int waitCount = 0;
                while (!container.isRunning() && waitCount < 50) {
                    TimeUnit.MILLISECONDS.sleep(100);
                    waitCount++;
                }

                listenerContainers.put(listenerKey, container);

                log.info("创建并启动监听器: queue={}, key={}, 运行状态={}",
                        queueName, listenerKey, container.isRunning());
            } catch (Exception e) {
                log.error("创建监听器失败: queue={}, key={}", queueName, listenerKey, e);
                throw new RuntimeException("创建监听器失败: " + e.getMessage(), e);
            }
        } finally {
            lock.unlock();
        }
    }

    public void stopAndRemoveListener(String queueName) {
        stopListenerByKey(queueName);
        stopListenerByKey(queueName + ".dlq.listener");
    }

    private void stopListenerByKey(String listenerKey) {
        ReentrantLock lock = listenerLocks.computeIfAbsent(listenerKey, k -> new ReentrantLock());
        lock.lock();
        try {
            SimpleMessageListenerContainer container = listenerContainers.remove(listenerKey);
            if (container != null) {
                try {
                    if (container.isRunning()) {
                        container.stop();
                        int waitCount = 0;
                        while (container.isRunning() && waitCount < 50) {
                            TimeUnit.MILLISECONDS.sleep(100);
                            waitCount++;
                        }
                    }
                    container.shutdown();
                    log.info("停止并移除监听器: key={}, 运行状态={}", listenerKey, container.isRunning());
                } catch (Exception e) {
                    log.error("停止监听器失败: key={}", listenerKey, e);
                    try {
                        container.shutdown();
                    } catch (Exception ex) {
                        log.error("强制关闭监听器失败: key={}", listenerKey, ex);
                    }
                }
            } else {
                log.warn("监听器不存在，跳过停止: key={}", listenerKey);
            }
        } finally {
            lock.unlock();
        }
    }

    public void restartListener(QueueConfig config) {
        String queueName = config.getQueueName();
        log.info("开始重启监听器: {}", queueName);

        stopAndRemoveListener(queueName);

        try {
            TimeUnit.MILLISECONDS.sleep(500);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }

        createAndStartListener(config);

        log.info("监听器重启完成: {}", queueName);
    }

    public boolean isListenerRunning(String queueName) {
        SimpleMessageListenerContainer container = listenerContainers.get(queueName);
        return container != null && container.isRunning();
    }

    public Map<String, Boolean> getAllListenerStatus() {
        Map<String, Boolean> status = new ConcurrentHashMap<>();
        listenerContainers.forEach((key, container) -> {
            status.put(key, container.isRunning());
        });
        return status;
    }
}
