package com.task.scheduler.entity;

import lombok.Data;
import javax.persistence.*;
import java.time.LocalDateTime;

@Data
@Entity
@Table(name = "task_alert")
public class TaskAlert {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false)
    private Long taskId;

    @Column(nullable = false)
    private String taskName;

    @Column(nullable = false)
    private String alertType;

    @Column(nullable = false)
    private Integer alertLevel;

    @Column(columnDefinition = "TEXT")
    private String alertMessage;

    @Column(columnDefinition = "TEXT")
    private String alertDetail;

    @Column(nullable = false)
    private Integer status = 0;

    @Column(nullable = false)
    private LocalDateTime createTime = LocalDateTime.now();

    private LocalDateTime handleTime;

    private String handleBy;

    @Column(columnDefinition = "TEXT")
    private String handleRemark;
}
