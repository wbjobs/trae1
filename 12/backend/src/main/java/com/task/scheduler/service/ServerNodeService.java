package com.task.scheduler.service;

import com.task.scheduler.entity.ServerNode;
import com.task.scheduler.repository.ServerNodeRepository;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.List;

@Service
public class ServerNodeService {

    @Autowired
    private ServerNodeRepository serverNodeRepository;

    public List<ServerNode> getAllServers() {
        return serverNodeRepository.findByDeleted(0);
    }

    public ServerNode getServerById(Long id) {
        return serverNodeRepository.findById(id).orElse(null);
    }

    public ServerNode getServerByName(String serverName) {
        return serverNodeRepository.findByServerNameAndDeleted(serverName, 0);
    }

    public ServerNode createServer(ServerNode serverNode) {
        serverNode.setCreateTime(LocalDateTime.now());
        serverNode.setUpdateTime(LocalDateTime.now());
        serverNode.setStatus(0);
        serverNode.setDeleted(0);
        return serverNodeRepository.save(serverNode);
    }

    public ServerNode updateServer(Long id, ServerNode serverNode) {
        ServerNode existing = serverNodeRepository.findById(id).orElse(null);
        if (existing == null) {
            return null;
        }
        existing.setServerName(serverNode.getServerName());
        existing.setIpAddress(serverNode.getIpAddress());
        existing.setPort(serverNode.getPort());
        existing.setUsername(serverNode.getUsername());
        existing.setPassword(serverNode.getPassword());
        existing.setSshKey(serverNode.getSshKey());
        existing.setOsType(serverNode.getOsType());
        existing.setTags(serverNode.getTags());
        existing.setRemark(serverNode.getRemark());
        existing.setUpdateTime(LocalDateTime.now());
        return serverNodeRepository.save(existing);
    }

    public void deleteServer(Long id) {
        ServerNode serverNode = serverNodeRepository.findById(id).orElse(null);
        if (serverNode != null) {
            serverNode.setDeleted(1);
            serverNode.setUpdateTime(LocalDateTime.now());
            serverNodeRepository.save(serverNode);
        }
    }

    public List<ServerNode> getActiveServers() {
        return serverNodeRepository.findActiveServers();
    }

    public ServerNode updateServerStatus(Long id, Integer status) {
        ServerNode serverNode = serverNodeRepository.findById(id).orElse(null);
        if (serverNode != null) {
            serverNode.setStatus(status);
            serverNode.setUpdateTime(LocalDateTime.now());
            if (status == 1) {
                serverNode.setLastHeartbeat(LocalDateTime.now());
            }
            return serverNodeRepository.save(serverNode);
        }
        return null;
    }

    public ServerNode updateServerMetrics(Long id, Integer cpuUsage, Integer memoryUsage, Integer diskUsage) {
        ServerNode serverNode = serverNodeRepository.findById(id).orElse(null);
        if (serverNode != null) {
            serverNode.setCpuUsage(cpuUsage);
            serverNode.setMemoryUsage(memoryUsage);
            serverNode.setDiskUsage(diskUsage);
            serverNode.setLastHeartbeat(LocalDateTime.now());
            return serverNodeRepository.save(serverNode);
        }
        return null;
    }
}
