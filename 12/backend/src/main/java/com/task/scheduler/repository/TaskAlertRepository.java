package com.task.scheduler.repository;

import com.task.scheduler.entity.TaskAlert;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.List;

@Repository
public interface TaskAlertRepository extends JpaRepository<TaskAlert, Long> {

    Page<TaskAlert> findByStatusOrderByCreateTimeDesc(Integer status, Pageable pageable);

    Page<TaskAlert> findAllByOrderByCreateTimeDesc(Pageable pageable);

    List<TaskAlert> findByStatus(Integer status);

    @Query("SELECT COUNT(a) FROM TaskAlert a WHERE a.status = ?1")
    Long countByStatus(Integer status);

    @Query("SELECT COUNT(a) FROM TaskAlert a WHERE a.alertLevel = ?1 AND a.status = 0")
    Long countByAlertLevelAndStatus(Integer alertLevel, Integer status);

    List<TaskAlert> findByTaskIdAndStatus(Long taskId, Integer status);

    Page<TaskAlert> findByAlertLevel(Integer alertLevel, Pageable pageable);

    @Query("SELECT a FROM TaskAlert a WHERE a.createTime BETWEEN ?1 AND ?2 ORDER BY a.createTime DESC")
    List<TaskAlert> findByTimeRange(LocalDateTime startTime, LocalDateTime endTime);
}
