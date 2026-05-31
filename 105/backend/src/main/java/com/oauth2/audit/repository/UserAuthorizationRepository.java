package com.oauth2.audit.repository;

import com.oauth2.audit.entity.UserAuthorization;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.List;
import java.util.Optional;

@Repository
public interface UserAuthorizationRepository extends JpaRepository<UserAuthorization, Long> {
    List<UserAuthorization> findByUserIdAndActiveTrue(Long userId);
    List<UserAuthorization> findByClientApplicationIdAndActiveTrue(Long clientId);
    Optional<UserAuthorization> findByUserIdAndClientApplicationIdAndActiveTrue(Long userId, Long clientId);
    List<UserAuthorization> findByUserId(Long userId);
}
