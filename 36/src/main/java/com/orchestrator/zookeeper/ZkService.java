package com.orchestrator.zookeeper;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.apache.zookeeper.*;
import org.apache.zookeeper.data.Stat;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public class ZkService {

    private static final Logger logger = LoggerFactory.getLogger(ZkService.class);
    private static final int SESSION_TIMEOUT = 10000;

    private final String connectString;
    private final ObjectMapper objectMapper;
    private ZooKeeper zooKeeper;

    public ZkService(String connectString) {
        this.connectString = connectString;
        this.objectMapper = new ObjectMapper();
    }

    public void connect() throws IOException, InterruptedException {
        CountDownLatch latch = new CountDownLatch(1);
        this.zooKeeper = new ZooKeeper(connectString, SESSION_TIMEOUT, event -> {
            if (event.getState() == Watcher.Event.KeeperState.SyncConnected) {
                latch.countDown();
            }
        });
        if (!latch.await(30, TimeUnit.SECONDS)) {
            throw new IOException("Failed to connect to ZooKeeper within timeout");
        }
        logger.info("Connected to ZooKeeper: {}", connectString);
        ensureBasePaths();
    }

    public void connect(Watcher watcher) throws IOException, InterruptedException {
        this.zooKeeper = new ZooKeeper(connectString, SESSION_TIMEOUT, watcher);
        logger.info("Connected to ZooKeeper with custom watcher: {}", connectString);
        ensureBasePaths();
    }

    private void ensureBasePaths() {
        ensurePath(ZkPaths.ROOT);
        ensurePath(ZkPaths.DAGS);
        ensurePath(ZkPaths.MASTERS);
        ensurePath(ZkPaths.WORKERS);
        ensurePath(ZkPaths.RUNNING_TASKS);
        ensurePath(ZkPaths.RESULTS);
    }

    public void ensurePath(String path) {
        try {
            if (zooKeeper.exists(path, false) == null) {
                zooKeeper.create(path, new byte[0], ZooDefs.Ids.OPEN_ACL_UNSAFE, CreateMode.PERSISTENT);
                logger.debug("Created path: {}", path);
            }
        } catch (KeeperException | InterruptedException e) {
            logger.error("Error ensuring path {}: {}", path, e.getMessage());
        }
    }

    public void createPersistent(String path, Object data) {
        try {
            byte[] bytes = data instanceof String ? ((String) data).getBytes() : objectMapper.writeValueAsBytes(data);
            if (zooKeeper.exists(path, false) == null) {
                zooKeeper.create(path, bytes, ZooDefs.Ids.OPEN_ACL_UNSAFE, CreateMode.PERSISTENT);
                logger.debug("Created persistent node: {}", path);
            } else {
                zooKeeper.setData(path, bytes, -1);
                logger.debug("Updated persistent node: {}", path);
            }
        } catch (KeeperException | InterruptedException | JsonProcessingException e) {
            logger.error("Error creating persistent node {}: {}", path, e.getMessage());
        }
    }

    public String createEphemeralSequential(String path, Object data) {
        try {
            byte[] bytes = data instanceof String ? ((String) data).getBytes() : objectMapper.writeValueAsBytes(data);
            return zooKeeper.create(path, bytes, ZooDefs.Ids.OPEN_ACL_UNSAFE, CreateMode.EPHEMERAL_SEQUENTIAL);
        } catch (KeeperException | InterruptedException | JsonProcessingException e) {
            logger.error("Error creating ephemeral sequential node {}: {}", path, e.getMessage());
            return null;
        }
    }

    public String createEphemeral(String path, Object data) {
        try {
            byte[] bytes = data instanceof String ? ((String) data).getBytes() : objectMapper.writeValueAsBytes(data);
            return zooKeeper.create(path, bytes, ZooDefs.Ids.OPEN_ACL_UNSAFE, CreateMode.EPHEMERAL);
        } catch (KeeperException | InterruptedException | JsonProcessingException e) {
            logger.error("Error creating ephemeral node {}: {}", path, e.getMessage());
            return null;
        }
    }

    public boolean createEphemeralIfAbsent(String path, Object data) {
        try {
            byte[] bytes = data instanceof String ? ((String) data).getBytes() : objectMapper.writeValueAsBytes(data);
            if (zooKeeper.exists(path, false) == null) {
                zooKeeper.create(path, bytes, ZooDefs.Ids.OPEN_ACL_UNSAFE, CreateMode.EPHEMERAL);
                return true;
            }
            return false;
        } catch (KeeperException | InterruptedException | JsonProcessingException e) {
            logger.error("Error creating ephemeral node if absent {}: {}", path, e.getMessage());
            return false;
        }
    }

    public <T> T getData(String path, Class<T> clazz) {
        try {
            byte[] data = zooKeeper.getData(path, false, null);
            if (data == null || data.length == 0) return null;
            if (clazz == String.class) {
                return clazz.cast(new String(data));
            }
            return objectMapper.readValue(data, clazz);
        } catch (KeeperException | InterruptedException | IOException e) {
            logger.error("Error getting data from {}: {}", path, e.getMessage());
            return null;
        }
    }

    public void setData(String path, Object data) {
        try {
            byte[] bytes = data instanceof String ? ((String) data).getBytes() : objectMapper.writeValueAsBytes(data);
            zooKeeper.setData(path, bytes, -1);
        } catch (KeeperException | InterruptedException | JsonProcessingException e) {
            logger.error("Error setting data for {}: {}", path, e.getMessage());
        }
    }

    public boolean exists(String path) {
        try {
            return zooKeeper.exists(path, false) != null;
        } catch (KeeperException | InterruptedException e) {
            logger.error("Error checking existence of {}: {}", path, e.getMessage());
            return false;
        }
    }

    public boolean exists(String path, Watcher watcher) {
        try {
            return zooKeeper.exists(path, watcher) != null;
        } catch (KeeperException | InterruptedException e) {
            logger.error("Error checking existence of {}: {}", path, e.getMessage());
            return false;
        }
    }

    public void delete(String path) {
        try {
            zooKeeper.delete(path, -1);
        } catch (KeeperException | InterruptedException e) {
            logger.error("Error deleting {}: {}", path, e.getMessage());
        }
    }

    public List<String> getChildren(String path) {
        try {
            List<String> children = zooKeeper.getChildren(path, false);
            return children != null ? children : Collections.emptyList();
        } catch (KeeperException | InterruptedException e) {
            logger.error("Error getting children of {}: {}", path, e.getMessage());
            return Collections.emptyList();
        }
    }

    public List<String> getChildren(String path, Watcher watcher) {
        try {
            List<String> children = zooKeeper.getChildren(path, watcher);
            return children != null ? children : Collections.emptyList();
        } catch (KeeperException | InterruptedException e) {
            logger.error("Error getting children of {} with watcher: {}", path, e.getMessage());
            return Collections.emptyList();
        }
    }

    public void addWatch(String path, Watcher watcher) {
        try {
            zooKeeper.exists(path, watcher);
        } catch (KeeperException | InterruptedException e) {
            logger.error("Error adding watch to {}: {}", path, e.getMessage());
        }
    }

    public ZooKeeper getZooKeeper() {
        return zooKeeper;
    }

    public ObjectMapper getObjectMapper() {
        return objectMapper;
    }

    public void close() {
        if (zooKeeper != null) {
            try {
                zooKeeper.close();
                logger.info("ZooKeeper connection closed");
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }
}
