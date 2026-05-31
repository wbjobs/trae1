package com.dql.mq.listener;

import com.dql.mq.handler.DeadLetterHandler;
import com.rabbitmq.client.Channel;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.amqp.core.Message;
import org.springframework.stereotype.Component;

@Slf4j
@Component
@RequiredArgsConstructor
public class DeadLetterListener {

    private final DeadLetterHandler deadLetterHandler;

    public void onMessage(Message message, Channel channel) throws Exception {
        log.debug("接收到死信消息: queue={}, messageId={}",
                message.getMessageProperties().getConsumerQueue(),
                message.getMessageProperties().getMessageId());
        deadLetterHandler.handleDeadLetter(message, channel);
    }
}
