package com.task.scheduler.repository;

import com.task.scheduler.entity.TaskConfig;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.stereotype.Repository;

import java.util.List;

@Repository
public interface TaskConfigRepository extends JpaRepository<TaskConfig, Long> {

    List<TaskConfig> findByDeleted(Integer deleted);

    @Query("SELECT t FROM TaskConfig t WHERE t.deleted = 0 AND t.status = 1")
    List<TaskConfig> findActiveTasks();

    TaskConfig findByTaskNameAndDeleted(String taskName, Integer deleted);

    @Query("SELECT t FROM TaskConfig t WHERE t.deleted = 0 AND t.targetServer = ?1")
    List<TaskConfig> findByTargetServer(String targetServer);
}
