package com.orchestrator;

import com.orchestrator.worker.WorkerNode;
import com.orchestrator.zookeeper.ZkService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class WorkerMain {

    private static final Logger logger = LoggerFactory.getLogger(WorkerMain.class);

    public static void main(String[] args) {
        String zkConnect = args.length > 0 ? args[0] : "127.0.0.1:2181";

        try {
            ZkService zkService = new ZkService(zkConnect);
            zkService.connect();

            WorkerNode worker = new WorkerNode(zkService);
            worker.start();

            logger.info("Worker started, press Ctrl+C to stop");
            Runtime.getRuntime().addShutdownHook(new Thread(worker::shutdown));

            Thread.currentThread().join();
        } catch (Exception e) {
            logger.error("Worker failed: {}", e.getMessage(), e);
        }
    }
}
