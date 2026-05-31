package com.alibaba.polardb.index.canal;

import com.alibaba.otter.canal.client.CanalConnectors;
import com.alibaba.polardb.index.config.ShardConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.List;

public class CanalConnector {

    private static final Logger logger = LoggerFactory.getLogger(CanalConnector.class);

    private final ShardConfig shardConfig;
    private volatile com.alibaba.otter.canal.client.CanalConnector connector;
    private volatile boolean running = false;

    public CanalConnector(ShardConfig shardConfig) {
        this.shardConfig = shardConfig;
    }

    public synchronized void connect() {
        if (running && connector != null) {
            return;
        }

        try {
            String[] servers = shardConfig.getCanalServer().split(",");
            List<InetSocketAddress> addresses = new ArrayList<>();
            for (String server : servers) {
                String[] parts = server.split(":");
                addresses.add(new InetSocketAddress(parts[0].trim(), Integer.parseInt(parts[1].trim())));
            }

            if (addresses.size() == 1) {
                connector = CanalConnectors.newSingleConnector(
                        addresses.get(0),
                        shardConfig.getDestination(),
                        shardConfig.getUsername(),
                        shardConfig.getPassword()
                );
            } else {
                connector = CanalConnectors.newClusterConnector(
                        addresses,
                        shardConfig.getDestination(),
                        shardConfig.getUsername(),
                        shardConfig.getPassword()
                );
            }

            connector.connect();
            running = true;
            logger.info("Connected to Canal server for shard: {}, destination: {}",
                    shardConfig.getId(), shardConfig.getDestination());
        } catch (Exception e) {
            logger.error("Failed to connect to Canal server for shard: {}", shardConfig.getId(), e);
            throw new RuntimeException("Failed to connect to Canal server", e);
        }
    }

    public void subscribe(String filter) {
        if (connector != null) {
            connector.subscribe(filter);
            logger.info("Subscribed to Canal with filter: {} for shard: {}", filter, shardConfig.getId());
        }
    }

    public void rollback() {
        if (connector != null) {
            connector.rollback();
        }
    }

    public com.alibaba.otter.canal.protocol.Message getWithoutAck(int batchSize) {
        if (connector != null) {
            return connector.getWithoutAck(batchSize);
        }
        return null;
    }

    public void ack(long batchId) {
        if (connector != null) {
            connector.ack(batchId);
        }
    }

    public void rollback(long batchId) {
        if (connector != null) {
            connector.rollback(batchId);
        }
    }

    public synchronized void disconnect() {
        if (connector != null) {
            try {
                connector.disconnect();
            } catch (Exception e) {
                logger.warn("Error disconnecting Canal connector for shard: {}", shardConfig.getId(), e);
            }
            running = false;
            logger.info("Disconnected from Canal server for shard: {}", shardConfig.getId());
        }
    }

    public boolean isRunning() {
        return running;
    }

    public synchronized void reconnect() {
        logger.info("Reconnecting to Canal server for shard: {}", shardConfig.getId());
        disconnect();
        connect();
    }
}
