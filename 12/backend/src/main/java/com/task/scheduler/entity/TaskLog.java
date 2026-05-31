package com.task.scheduler.entity;

import lombok.Data;
import javax.persistence.*;
import java.time.LocalDateTime;

@Data
@Entity
@Table(name = "task_log")
public class TaskLog {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false)
    private Long taskId;

    @Column(nullable = false)
    private String taskName;

    @Column(nullable = false)
    private String serverName;

    @Column(nullable = false)
    private Integer executeStatus;

    @Column(columnDefinition = "TEXT")
    private String executeResult;

    @Column(columnDefinition = "TEXT")
    private String errorMessage;

    private LocalDateTime startTime;

    private LocalDateTime endTime;

    private Long duration;

    private Integer retryAttempts = 0;

    @Column(nullable = false)
    private LocalDateTime createTime = LocalDateTime.now();
}
