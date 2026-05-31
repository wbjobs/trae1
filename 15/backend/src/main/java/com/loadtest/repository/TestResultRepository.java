package com.loadtest.repository;

import com.loadtest.entity.TestResult;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;
import java.util.List;

@Repository
public interface TestResultRepository extends JpaRepository<TestResult, Long> {
    List<TestResult> findByTaskIdOrderByTimestampAsc(Long taskId);

    @Query("SELECT r FROM TestResult r WHERE r.taskId = :taskId ORDER BY r.timestamp ASC")
    List<TestResult> findByTaskId(@Param("taskId") Long taskId);

    @Query("SELECT COUNT(r) FROM TestResult r WHERE r.taskId = :taskId AND r.success = true")
    Long countSuccessByTaskId(@Param("taskId") Long taskId);

    @Query("SELECT COUNT(r) FROM TestResult r WHERE r.taskId = :taskId AND r.success = false")
    Long countFailureByTaskId(@Param("taskId") Long taskId);

    @Query("SELECT AVG(r.elapsed) FROM TestResult r WHERE r.taskId = :taskId")
    Double avgElapsedByTaskId(@Param("taskId") Long taskId);

    @Query("SELECT MIN(r.elapsed) FROM TestResult r WHERE r.taskId = :taskId")
    Long minElapsedByTaskId(@Param("taskId") Long taskId);

    @Query("SELECT MAX(r.elapsed) FROM TestResult r WHERE r.taskId = :taskId")
    Long maxElapsedByTaskId(@Param("taskId") Long taskId);
}
