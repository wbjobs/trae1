package com.task.scheduler.repository;

import com.task.scheduler.entity.TaskLog;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.List;

@Repository
public interface TaskLogRepository extends JpaRepository<TaskLog, Long> {

    Page<TaskLog> findByTaskIdOrderByCreateTimeDesc(Long taskId, Pageable pageable);

    Page<TaskLog> findAllByOrderByCreateTimeDesc(Pageable pageable);

    List<TaskLog> findByTaskIdAndExecuteStatus(Long taskId, Integer executeStatus);

    @Query("SELECT COUNT(t) FROM TaskLog t WHERE t.taskId = ?1 AND t.createTime BETWEEN ?2 AND ?3")
    Long countByTaskIdAndTimeRange(Long taskId, LocalDateTime startTime, LocalDateTime endTime);

    @Query("SELECT t FROM TaskLog t WHERE t.executeStatus = 0 AND t.retryAttempts < ?1 ORDER BY t.createTime ASC")
    List<TaskLog> findFailedTasksForRetry(Integer maxRetryAttempts);
}
