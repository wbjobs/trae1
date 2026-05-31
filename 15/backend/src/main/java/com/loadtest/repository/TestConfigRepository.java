package com.loadtest.repository;

import com.loadtest.entity.TestConfig;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;
import java.util.List;

@Repository
public interface TestConfigRepository extends JpaRepository<TestConfig, Long> {
    List<TestConfig> findAllByOrderByCreatedAtDesc();
}
