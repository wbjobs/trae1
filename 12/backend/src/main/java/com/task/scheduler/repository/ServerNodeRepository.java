package com.task.scheduler.repository;

import com.task.scheduler.entity.ServerNode;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.stereotype.Repository;

import java.util.List;

@Repository
public interface ServerNodeRepository extends JpaRepository<ServerNode, Long> {

    List<ServerNode> findByDeleted(Integer deleted);

    @Query("SELECT s FROM ServerNode s WHERE s.deleted = 0 AND s.status = 1")
    List<ServerNode> findActiveServers();

    ServerNode findByServerNameAndDeleted(String serverName, Integer deleted);

    ServerNode findByIpAddressAndDeleted(String ipAddress, Integer deleted);
}
