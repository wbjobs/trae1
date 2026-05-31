package com.task.scheduler.controller;

import com.task.scheduler.common.Result;
import com.task.scheduler.entity.ServerNode;
import com.task.scheduler.service.ServerNodeService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@RestController
@RequestMapping("/servers")
public class ServerNodeController {

    @Autowired
    private ServerNodeService serverNodeService;

    @GetMapping
    public Result<List<ServerNode>> getAllServers() {
        return Result.success(serverNodeService.getAllServers());
    }

    @GetMapping("/{id}")
    public Result<ServerNode> getServerById(@PathVariable Long id) {
        ServerNode serverNode = serverNodeService.getServerById(id);
        if (serverNode == null) {
            return Result.error("Server not found");
        }
        return Result.success(serverNode);
    }

    @PostMapping
    public Result<ServerNode> createServer(@RequestBody ServerNode serverNode) {
        return Result.success(serverNodeService.createServer(serverNode));
    }

    @PutMapping("/{id}")
    public Result<ServerNode> updateServer(@PathVariable Long id, @RequestBody ServerNode serverNode) {
        ServerNode updated = serverNodeService.updateServer(id, serverNode);
        if (updated == null) {
            return Result.error("Server not found");
        }
        return Result.success(updated);
    }

    @DeleteMapping("/{id}")
    public Result<Void> deleteServer(@PathVariable Long id) {
        serverNodeService.deleteServer(id);
        return Result.success();
    }

    @GetMapping("/active")
    public Result<List<ServerNode>> getActiveServers() {
        return Result.success(serverNodeService.getActiveServers());
    }

    @PostMapping("/{id}/status")
    public Result<ServerNode> updateServerStatus(@PathVariable Long id, @RequestParam Integer status) {
        ServerNode updated = serverNodeService.updateServerStatus(id, status);
        if (updated == null) {
            return Result.error("Server not found");
        }
        return Result.success(updated);
    }

    @PostMapping("/{id}/metrics")
    public Result<ServerNode> updateServerMetrics(
            @PathVariable Long id,
            @RequestParam Integer cpuUsage,
            @RequestParam Integer memoryUsage,
            @RequestParam Integer diskUsage) {
        ServerNode updated = serverNodeService.updateServerMetrics(id, cpuUsage, memoryUsage, diskUsage);
        if (updated == null) {
            return Result.error("Server not found");
        }
        return Result.success(updated);
    }
}
