package com.task.scheduler.service;

import com.task.scheduler.entity.ServerNode;
import com.task.scheduler.repository.ServerNodeRepository;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.InetAddress;
import java.time.LocalDateTime;
import java.util.List;

@Service
public class ServerMonitorService {

    private static final Logger logger = LoggerFactory.getLogger(ServerMonitorService.class);

    @Autowired
    private ServerNodeRepository serverNodeRepository;

    @Scheduled(fixedRate = 60000)
    public void monitorServers() {
        List<ServerNode> servers = serverNodeRepository.findByDeleted(0);
        for (ServerNode server : servers) {
            try {
                boolean reachable = checkServerReachable(server);
                if (reachable) {
                    server.setStatus(1);
                    server.setLastHeartbeat(LocalDateTime.now());
                    collectServerMetrics(server);
                } else {
                    server.setStatus(0);
                }
                serverNodeRepository.save(server);
            } catch (Exception e) {
                logger.error("Failed to monitor server {}: {}", server.getServerName(), e.getMessage());
                server.setStatus(0);
                serverNodeRepository.save(server);
            }
        }
    }

    private boolean checkServerReachable(ServerNode server) {
        try {
            InetAddress address = InetAddress.getByName(server.getIpAddress());
            return address.isReachable(5000);
        } catch (Exception e) {
            logger.warn("Server {} is not reachable: {}", server.getServerName(), e.getMessage());
            return false;
        }
    }

    private void collectServerMetrics(ServerNode server) {
        try {
            if ("Linux".equalsIgnoreCase(server.getOsType()) || "Unix".equalsIgnoreCase(server.getOsType())) {
                collectLinuxMetrics(server);
            } else if ("Windows".equalsIgnoreCase(server.getOsType())) {
                collectWindowsMetrics(server);
            }
        } catch (Exception e) {
            logger.warn("Failed to collect metrics for server {}: {}", server.getServerName(), e.getMessage());
        }
    }

    private void collectLinuxMetrics(ServerNode server) {
        try {
            String cpuUsage = executeRemoteCommand(server,
                    "top -bn1 | grep 'Cpu(s)' | awk '{print $2}' | cut -d. -f1");
            if (cpuUsage != null && !cpuUsage.trim().isEmpty()) {
                server.setCpuUsage(Integer.parseInt(cpuUsage.trim()));
            }

            String memUsage = executeRemoteCommand(server,
                    "free | grep Mem | awk '{printf \"%.0f\", $3/$2 * 100}'");
            if (memUsage != null && !memUsage.trim().isEmpty()) {
                server.setMemoryUsage(Integer.parseInt(memUsage.trim()));
            }

            String diskUsage = executeRemoteCommand(server,
                    "df / | tail -1 | awk '{print $5}' | tr -d '%'");
            if (diskUsage != null && !diskUsage.trim().isEmpty()) {
                server.setDiskUsage(Integer.parseInt(diskUsage.trim()));
            }
        } catch (Exception e) {
            logger.warn("Failed to collect Linux metrics for {}: {}", server.getServerName(), e.getMessage());
        }
    }

    private void collectWindowsMetrics(ServerNode server) {
        try {
            String cpuUsage = executeRemoteCommand(server,
                    "wmic cpu get loadvalue | findstr /r '[0-9]'");
            if (cpuUsage != null && !cpuUsage.trim().isEmpty()) {
                server.setCpuUsage(Integer.parseInt(cpuUsage.trim()));
            }

            String memUsage = executeRemoteCommand(server,
                    "wmic OS get FreePhysicalMemory,TotalVisibleMemorySize | findstr /r '[0-9]'");
            if (memUsage != null && !memUsage.trim().isEmpty()) {
                String[] parts = memUsage.trim().split("\\s+");
                if (parts.length >= 2) {
                    long freeMem = Long.parseLong(parts[0]);
                    long totalMem = Long.parseLong(parts[1]);
                    int usedPercent = (int) ((totalMem - freeMem) * 100 / totalMem);
                    server.setMemoryUsage(usedPercent);
                }
            }

            String diskUsage = executeRemoteCommand(server,
                    "wmic logicaldisk get size,freespace,caption | findstr /r 'C:'");
            if (diskUsage != null && !diskUsage.trim().isEmpty()) {
                String[] parts = diskUsage.trim().split("\\s+");
                if (parts.length >= 3) {
                    long freeSpace = Long.parseLong(parts[1]);
                    long totalSize = Long.parseLong(parts[2]);
                    int usedPercent = (int) ((totalSize - freeSpace) * 100 / totalSize);
                    server.setDiskUsage(usedPercent);
                }
            }
        } catch (Exception e) {
            logger.warn("Failed to collect Windows metrics for {}: {}", server.getServerName(), e.getMessage());
        }
    }

    private String executeRemoteCommand(ServerNode server, String command) {
        try {
            ProcessBuilder processBuilder = new ProcessBuilder(
                    "ssh",
                    "-o", "StrictHostKeyChecking=no",
                    "-o", "ConnectTimeout=10",
                    "-p", String.valueOf(server.getPort()),
                    server.getUsername() + "@" + server.getIpAddress(),
                    command
            );

            Process process = processBuilder.start();
            BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()));
            StringBuilder result = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                result.append(line).append("\n");
            }
            process.waitFor(10, java.util.concurrent.TimeUnit.SECONDS);
            return result.toString().trim();
        } catch (Exception e) {
            logger.warn("Remote command execution failed for server {}: {}", server.getServerName(), e.getMessage());
            return null;
        }
    }

    public boolean testServerConnection(Long serverId) {
        ServerNode server = serverNodeRepository.findById(serverId).orElse(null);
        if (server == null) {
            return false;
        }
        return checkServerReachable(server);
    }

    public ServerNode getServerStatus(Long serverId) {
        ServerNode server = serverNodeRepository.findById(serverId).orElse(null);
        if (server != null && server.getStatus() == 1) {
            collectServerMetrics(server);
            serverNodeRepository.save(server);
        }
        return server;
    }
}
