package com.oauth2.audit.config;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

@Configuration
@ConfigurationProperties(prefix = "ml")
@Data
public class MLConfig {
    private AnomalyDetection anomalyDetection = new AnomalyDetection();
    private Notification notification = new Notification();

    @Data
    public static class AnomalyDetection {
        private boolean enabled = true;
        private double threshold = 0.7;
        private int trainingDays = 90;
        private String updateCron = "0 0 2 ? * SUN";
    }

    @Data
    public static class Notification {
        private Sms sms = new Sms();
        private Email email = new Email();
    }

    @Data
    public static class Sms {
        private boolean enabled = false;
        private String provider = "twilio";
        private String fromNumber;
    }

    @Data
    public static class Email {
        private boolean enabled = true;
        private String smtpHost;
        private int smtpPort = 587;
        private String fromAddress;
    }
}
