package com.dql.mq.repository;

import com.dql.mq.entity.DeadMessage;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.JpaSpecificationExecutor;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Optional;

@Repository
public interface DeadMessageRepository extends JpaRepository<DeadMessage, Long>, JpaSpecificationExecutor<DeadMessage> {

    Optional<DeadMessage> findByMessageId(String messageId);

    Page<DeadMessage> findByQueueName(String queueName, Pageable pageable);

    Page<DeadMessage> findByStatus(String status, Pageable pageable);

    Page<DeadMessage> findByQueueNameAndStatus(String queueName, String status, Pageable pageable);

    Page<DeadMessage> findByStatusAndCreatedTimeBefore(String status, LocalDateTime createdTime, Pageable pageable);

    List<DeadMessage> findByStatusAndNextRetryTimeBefore(String status, LocalDateTime nextRetryTime);

    List<DeadMessage> findByStatusIn(List<String> statuses);

    Page<DeadMessage> findByCategory(String category, Pageable pageable);

    Page<DeadMessage> findByErrorType(String errorType, Pageable pageable);

    @Query("SELECT COUNT(d) FROM DeadMessage d WHERE d.queueName = :queueName AND d.status = :status")
    Long countByQueueNameAndStatus(@Param("queueName") String queueName, @Param("status") String status);

    @Query("SELECT COUNT(d) FROM DeadMessage d WHERE d.queueName = :queueName")
    Long countByQueueName(@Param("queueName") String queueName);

    @Query("SELECT COUNT(d) FROM DeadMessage d WHERE d.category = :category")
    Long countByCategory(@Param("category") String category);

    @Query("SELECT COUNT(d) FROM DeadMessage d WHERE d.errorType = :errorType")
    Long countByErrorType(@Param("errorType") String errorType);

    @Query("SELECT d.queueName, d.status, COUNT(d) FROM DeadMessage d GROUP BY d.queueName, d.status")
    List<Object[]> countGroupByQueueNameAndStatus();

    @Query("SELECT d.category, COUNT(d) FROM DeadMessage d GROUP BY d.category")
    List<Object[]> countGroupByCategory();

    @Query("SELECT d.errorType, COUNT(d) FROM DeadMessage d GROUP BY d.errorType")
    List<Object[]> countGroupByErrorType();

    @Query("SELECT d.category, d.status, COUNT(d) FROM DeadMessage d GROUP BY d.category, d.status")
    List<Object[]> countGroupByCategoryAndStatus();

    @Query("SELECT d.queueName, d.category, COUNT(d) FROM DeadMessage d GROUP BY d.queueName, d.category")
    List<Object[]> countGroupByQueueNameAndCategory();

    @Query("SELECT d.errorCode, COUNT(d) FROM DeadMessage d WHERE d.errorCode IS NOT NULL GROUP BY d.errorCode")
    List<Object[]> countGroupByErrorCode();

    @Query("SELECT d.errorMessage, COUNT(d) FROM DeadMessage d WHERE d.errorMessage IS NOT NULL GROUP BY d.errorMessage")
    List<Object[]> countGroupByErrorMessage();

    @Modifying
    @Query("UPDATE DeadMessage d SET d.status = :status, d.updatedTime = :updatedTime WHERE d.id = :id")
    int updateStatusById(@Param("id") Long id, @Param("status") String status, @Param("updatedTime") LocalDateTime updatedTime);

    @Modifying
    @Query("UPDATE DeadMessage d SET d.retryCount = d.retryCount + 1, d.status = :status, d.updatedTime = :updatedTime WHERE d.id = :id")
    int incrementRetryCountAndUpdateStatus(@Param("id") Long id, @Param("status") String status, @Param("updatedTime") LocalDateTime updatedTime);

    @Modifying
    @Query("DELETE FROM DeadMessage d WHERE d.id IN :ids")
    int deleteByIds(@Param("ids") List<Long> ids);

    @Modifying
    @Query("DELETE FROM DeadMessage d WHERE d.queueName = :queueName AND d.status = :status")
    int deleteByQueueNameAndStatus(@Param("queueName") String queueName, @Param("status") String status);

    @Query("SELECT d.queueName FROM DeadMessage d GROUP BY d.queueName")
    List<String> findDistinctQueueNames();
}
