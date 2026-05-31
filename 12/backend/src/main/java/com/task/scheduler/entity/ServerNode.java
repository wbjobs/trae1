package com.task.scheduler.entity;

import lombok.Data;
import javax.persistence.*;
import java.time.LocalDateTime;

@Data
@Entity
@Table(name = "server_node")
public class ServerNode {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false, unique = true)
    private String serverName;

    @Column(nullable = false)
    private String ipAddress;

    @Column(nullable = false)
    private Integer port = 22;

    @Column(nullable = false)
    private String username;

    private String password;

    private String sshKey;

    private String osType;

    private String tags;

    @Column(nullable = false)
    private Integer status = 0;

    private Integer cpuUsage;

    private Integer memoryUsage;

    private Integer diskUsage;

    private LocalDateTime lastHeartbeat;

    @Column(columnDefinition = "TEXT")
    private String remark;

    @Column(nullable = false)
    private LocalDateTime createTime = LocalDateTime.now();

    @Column(nullable = false)
    private LocalDateTime updateTime = LocalDateTime.now();

    @Column(nullable = false)
    private Integer deleted = 0;
}
