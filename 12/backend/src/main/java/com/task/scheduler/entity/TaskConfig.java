package com.task.scheduler.entity;

import lombok.Data;
import javax.persistence.*;
import java.time.LocalDateTime;

@Data
@Entity
@Table(name = "task_config")
public class TaskConfig {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false, unique = true)
    private String taskName;

    @Column(nullable = false)
    private String taskGroup;

    @Column(nullable = false)
    private String cronExpression;

    @Column(nullable = false)
    private String taskType;

    @Column(columnDefinition = "TEXT")
    private String taskParams;

    @Column(nullable = false)
    private String targetServer;

    @Column(nullable = false)
    private String executeCommand;

    @Column(columnDefinition = "TEXT")
    private String description;

    @Column(nullable = false)
    private Integer status = 0;

    @Column(nullable = false)
    private Integer retryCount = 3;

    @Column(nullable = false)
    private Integer retryInterval = 5;

    @Column(nullable = false)
    private Integer timeout = 300;

    private LocalDateTime lastExecuteTime;

    private LocalDateTime nextExecuteTime;

    private String lastExecuteResult;

    @Column(nullable = false)
    private LocalDateTime createTime = LocalDateTime.now();

    @Column(nullable = false)
    private LocalDateTime updateTime = LocalDateTime.now();

    @Column(nullable = false)
    private Integer deleted = 0;
}
