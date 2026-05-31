package com.oauth2.audit.entity;

import lombok.Data;
import org.springframework.data.annotation.Id;
import org.springframework.data.elasticsearch.annotations.Document;
import org.springframework.data.elasticsearch.annotations.Field;
import org.springframework.data.elasticsearch.annotations.FieldType;

import java.time.LocalDateTime;

@Document(indexName = "oauth2-audit-logs")
@Data
public class AuditLog {
    @Id
    private String id;

    @Field(type = FieldType.Keyword)
    private String userId;

    @Field(type = FieldType.Keyword)
    private String clientId;

    @Field(type = FieldType.Keyword)
    private String clientName;

    @Field(type = FieldType.Text)
    private String username;

    @Field(type = FieldType.Keyword)
    private String scope;

    @Field(type = FieldType.Keyword)
    private String resourcePath;

    @Field(type = FieldType.Keyword)
    private String httpMethod;

    @Field(type = FieldType.Date)
    private LocalDateTime timestamp;

    @Field(type = FieldType.Integer)
    private Integer httpStatus;

    @Field(type = FieldType.Boolean)
    private Boolean isAnomaly;

    @Field(type = FieldType.Text)
    private String anomalyType;
}
