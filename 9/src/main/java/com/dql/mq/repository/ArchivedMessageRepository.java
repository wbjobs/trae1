package com.dql.mq.repository;

import com.dql.mq.entity.ArchivedMessage;
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

@Repository
public interface ArchivedMessageRepository extends JpaRepository<ArchivedMessage, Long>, JpaSpecificationExecutor<ArchivedMessage> {

    Page<ArchivedMessage> findByQueueName(String queueName, Pageable pageable);

    Page<ArchivedMessage> findByQueueNameAndFinalStatus(String queueName, String finalStatus, Pageable pageable);

    List<ArchivedMessage> findByArchivedTimeBefore(LocalDateTime archivedTime);

    @Query("SELECT COUNT(a) FROM ArchivedMessage a WHERE a.queueName = :queueName")
    Long countByQueueName(@Param("queueName") String queueName);

    @Modifying
    @Query("DELETE FROM ArchivedMessage a WHERE a.id IN :ids")
    int deleteByIds(@Param("ids") List<Long> ids);

    @Modifying
    @Query("DELETE FROM ArchivedMessage a WHERE a.archivedTime < :beforeTime")
    int deleteByArchivedTimeBefore(@Param("beforeTime") LocalDateTime beforeTime);
}
