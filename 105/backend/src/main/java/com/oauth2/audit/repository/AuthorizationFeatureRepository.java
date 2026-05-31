package com.oauth2.audit.repository;

import com.oauth2.audit.entity.AuthorizationFeature;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.List;

@Repository
public interface AuthorizationFeatureRepository extends JpaRepository<AuthorizationFeature, Long> {
    List<AuthorizationFeature> findByUserId(Long userId);

    List<AuthorizationFeature> findByUserIdAndCreatedAtBetween(Long userId, LocalDateTime start, LocalDateTime end);

    @Query("SELECT af FROM AuthorizationFeature af WHERE af.createdAt >= :since ORDER BY af.createdAt DESC")
    List<AuthorizationFeature> findAllSince(@Param("since") LocalDateTime since);

    @Query("SELECT COUNT(af) FROM AuthorizationFeature af WHERE af.user.id = :userId AND af.createdAt >= :since")
    Integer countByUserIdSince(@Param("userId") Long userId, @Param("since") LocalDateTime since);

    @Query("SELECT DISTINCT af.clientId FROM AuthorizationFeature af WHERE af.user.id = :userId")
    List<Integer> findDistinctClientIdsByUserId(@Param("userId") Long userId);

    @Query("SELECT AVG(af.hourOfDay) FROM AuthorizationFeature af WHERE af.user.id = :userId")
    Double findAverageHourByUserId(@Param("userId") Long userId);

    @Query("SELECT af.clientId, COUNT(af) FROM AuthorizationFeature af WHERE af.user.id = :userId GROUP BY af.clientId ORDER BY COUNT(af) DESC")
    List<Object[]> findMostUsedClientsByUserId(@Param("userId") Long userId);
}
