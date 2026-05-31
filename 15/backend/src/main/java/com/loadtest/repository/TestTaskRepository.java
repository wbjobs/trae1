package com.loadtest.repository;

import com.loadtest.entity.TestTask;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;
import java.util.List;

@Repository
public interface TestTaskRepository extends JpaRepository<TestTask, Long> {
    List<TestTask> findAllByOrderByCreatedAtDesc();
    List<TestTask> findByStatusOrderByCreatedAtDesc(TestTask.TaskStatus status);
    List<TestTask> findByConfigIdOrderByCreatedAtDesc(Long configId);
}
