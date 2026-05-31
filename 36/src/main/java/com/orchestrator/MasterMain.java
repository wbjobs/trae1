package com.orchestrator;

import com.orchestrator.zookeeper.MasterScheduler;
import com.orchestrator.zookeeper.ZkService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class MasterMain {

    private static final Logger logger = LoggerFactory.getLogger(MasterMain.class);

    public static void main(String[] args) {
        String zkConnect = args.length > 0 ? args[0] : "127.0.0.1:2181";

        try {
            ZkService zkService = new ZkService(zkConnect);
            zkService.connect();

            MasterScheduler scheduler = new MasterScheduler(zkService);
            scheduler.start();

            logger.info("Master started, press Ctrl+C to stop");
            Runtime.getRuntime().addShutdownHook(new Thread(scheduler::shutdown));

            Thread.currentThread().join();
        } catch (Exception e) {
            logger.error("Master failed: {}", e.getMessage(), e);
        }
    }
}
