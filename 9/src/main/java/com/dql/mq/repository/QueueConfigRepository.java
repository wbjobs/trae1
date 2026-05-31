package com.dql.mq.repository;

import com.dql.mq.entity.QueueConfig;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Optional;

@Repository
public interface QueueConfigRepository extends JpaRepository<QueueConfig, Long> {

    Optional<QueueConfig> findByQueueName(String queueName);

    Optional<QueueConfig> findByExchangeNameAndRoutingKey(String exchangeName, String routingKey);

    List<QueueConfig> findByEnabledTrue();

    List<QueueConfig> findByEnabledTrueAndAutoDlqTrue();

    boolean existsByQueueName(String queueName);

    @Modifying
    @Query("UPDATE QueueConfig q SET q.enabled = :enabled, q.updatedTime = :updatedTime WHERE q.id = :id")
    int updateEnabledById(@Param("id") Long id, @Param("enabled") Boolean enabled, @Param("updatedTime") LocalDateTime updatedTime);

    @Modifying
    @Query("UPDATE QueueConfig q SET q.maxRetryCount = :maxRetryCount, q.updatedTime = :updatedTime WHERE q.id = :id")
    int updateMaxRetryCountById(@Param("id") Long id, @Param("maxRetryCount") Integer maxRetryCount, @Param("updatedTime") LocalDateTime updatedTime);
}
