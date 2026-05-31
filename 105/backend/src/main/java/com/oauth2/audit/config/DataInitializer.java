package com.oauth2.audit.config;

import com.oauth2.audit.entity.ClientApplication;
import com.oauth2.audit.entity.User;
import com.oauth2.audit.repository.ClientApplicationRepository;
import com.oauth2.audit.repository.UserRepository;
import lombok.RequiredArgsConstructor;
import org.springframework.boot.CommandLineRunner;
import org.springframework.stereotype.Component;

@Component
@RequiredArgsConstructor
public class DataInitializer implements CommandLineRunner {
    private final UserRepository userRepository;
    private final ClientApplicationRepository clientApplicationRepository;

    @Override
    public void run(String... args) {
        if (!userRepository.existsByUsername("admin")) {
            User admin = new User();
            admin.setUsername("admin");
            admin.setPassword("{noop}admin123");
            admin.setEmail("admin@example.com");
            admin.setName("Admin User");
            userRepository.save(admin);
        }

        if (!userRepository.existsByUsername("user1")) {
            User user1 = new User();
            user1.setUsername("user1");
            user1.setPassword("{noop}user123");
            user1.setEmail("user1@example.com");
            user1.setName("Test User 1");
            userRepository.save(user1);
        }

        if (!userRepository.existsByUsername("user2")) {
            User user2 = new User();
            user2.setUsername("user2");
            user2.setPassword("{noop}user123");
            user2.setEmail("user2@example.com");
            user2.setName("Test User 2");
            userRepository.save(user2);
        }

        if (!clientApplicationRepository.existsByClientId("demo-client")) {
            ClientApplication client = new ClientApplication();
            client.setClientId("demo-client");
            client.setClientSecret("{noop}demo-secret");
            client.setClientName("Demo Application");
            client.setRedirectUri("http://127.0.0.1:3000/callback");
            client.setDescription("A demo client application for OAuth2 testing");
            clientApplicationRepository.save(client);
        }

        if (!clientApplicationRepository.existsByClientId("analytics-app")) {
            ClientApplication analytics = new ClientApplication();
            analytics.setClientId("analytics-app");
            analytics.setClientSecret("{noop}analytics-secret");
            analytics.setClientName("Analytics Dashboard");
            analytics.setRedirectUri("http://127.0.0.1:3000/callback");
            analytics.setDescription("Analytics and reporting application");
            clientApplicationRepository.save(analytics);
        }
    }
}
