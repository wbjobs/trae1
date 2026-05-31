package com.clickstream;

import com.clickstream.model.ClickEvent;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import org.apache.kafka.clients.producer.KafkaProducer;
import org.apache.kafka.clients.producer.ProducerConfig;
import org.apache.kafka.clients.producer.ProducerRecord;
import org.apache.kafka.common.serialization.StringSerializer;

import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Properties;
import java.util.Random;
import java.util.UUID;

public class TestDataProducer {

    private static final String[] PAGES = {
            "/home", "/products", "/products/123", "/products/456",
            "/cart", "/checkout", "/payment", "/confirmation",
            "/about", "/contact", "/login", "/register",
            "/search", "/blog", "/blog/post-1", "/blog/post-2"
    };

    private static final String[] USERS = {
            "user-001", "user-002", "user-003", "user-004", "user-005",
            "user-006", "user-007", "user-008", "user-009", "user-010"
    };

    private static final String[] USER_AGENTS = {
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0",
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Safari/17.0",
            "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0) Mobile/15E148",
            "Mozilla/5.0 (Linux; Android 14) Chrome/120.0"
    };

    public static void main(String[] args) {
        Properties props = new Properties();
        props.put(ProducerConfig.BOOTSTRAP_SERVERS_CONFIG, "localhost:9092");
        props.put(ProducerConfig.KEY_SERIALIZER_CLASS_CONFIG, StringSerializer.class.getName());
        props.put(ProducerConfig.VALUE_SERIALIZER_CLASS_CONFIG, StringSerializer.class.getName());

        ObjectMapper mapper = new ObjectMapper();
        mapper.registerModule(new JavaTimeModule());
        mapper.disable(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS);

        KafkaProducer<String, String> producer = new KafkaProducer<>(props);
        Random random = new Random();

        try {
            int totalEvents = 100;
            System.out.println("Starting to produce " + totalEvents + " test events...");

            for (int i = 0; i < totalEvents; i++) {
                String userId = USERS[random.nextInt(USERS.length)];
                String pageUrl = PAGES[random.nextInt(PAGES.length)];
                String referer = random.nextInt(3) == 0 ? null : PAGES[random.nextInt(PAGES.length)];
                String userAgent = USER_AGENTS[random.nextInt(USER_AGENTS.length)];

                Instant timestamp = Instant.now()
                        .minus(random.nextInt(60), ChronoUnit.MINUTES)
                        .minus(random.nextInt(24), ChronoUnit.HOURS);

                ClickEvent event = ClickEvent.builder()
                        .userId(userId)
                        .pageUrl(pageUrl)
                        .timestamp(timestamp)
                        .referer(referer)
                        .userAgent(userAgent)
                        .build();

                String json = mapper.writeValueAsString(event);
                ProducerRecord<String, String> record = new ProducerRecord<>("click-events", userId, json);
                producer.send(record);

                System.out.printf("Sent event %d/%d: user=%s, page=%s%n", i + 1, totalEvents, userId, pageUrl);

                Thread.sleep(100);
            }

            System.out.println("All events sent successfully!");
        } catch (Exception e) {
            e.printStackTrace();
        } finally {
            producer.close();
        }
    }
}
