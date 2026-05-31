package com.oauth2.audit.entity;

import jakarta.persistence.*;
import lombok.Data;

@Entity
@Table(name = "client_applications")
@Data
public class ClientApplication {
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(unique = true, nullable = false)
    private String clientId;

    @Column(nullable = false)
    private String clientSecret;

    @Column(nullable = false)
    private String clientName;

    @Column(nullable = false)
    private String redirectUri;

    @Column(columnDefinition = "TEXT")
    private String description;
}
