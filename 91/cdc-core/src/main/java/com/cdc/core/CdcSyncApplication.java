package com.cdc.core;

import com.cdc.common.config.CdcConfig;
import com.cdc.common.util.ConfigLoader;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class CdcSyncApplication {

    private static final Logger logger = LoggerFactory.getLogger(CdcSyncApplication.class);

    private static CdcEngine engine;

    public static void main(String[] args) {
        String configPath = args.length > 0 ? args[0] : "config.yaml";

        try {
            logger.info("Loading configuration from: {}", configPath);
            CdcConfig config = ConfigLoader.loadConfig(configPath);

            engine = new CdcEngine(config);
            engine.start();

            while (engine.isRunning()) {
                try {
                    Thread.sleep(5000);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }

        } catch (Exception e) {
            logger.error("CDC Sync application failed", e);
            System.exit(1);
        } finally {
            try {
                if (engine != null) {
                    engine.stop();
                }
            } catch (Exception e) {
                logger.error("Error during engine shutdown", e);
            }
        }
    }

    public static CdcEngine getEngine() {
        return engine;
    }
}
