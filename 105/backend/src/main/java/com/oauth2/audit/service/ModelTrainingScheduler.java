package com.oauth2.audit.service;

import com.oauth2.audit.config.MLConfig;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.context.event.ApplicationReadyEvent;
import org.springframework.context.event.EventListener;
import org.springframework.scheduling.annotation.EnableScheduling;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.LocalDateTime;

@Component
@EnableScheduling
@RequiredArgsConstructor
@Slf4j
public class ModelTrainingScheduler {
    private final IsolationForestModelService modelService;
    private final FeatureExtractionService featureExtractionService;
    private final MLConfig mlConfig;

    @EventListener(ApplicationReadyEvent.class)
    public void onApplicationReady() {
        log.info("Application ready, triggering initial model training");
        trainModel();
    }

    @Scheduled(cron = "${ml.anomaly-detection.update-cron:0 0 2 ? * SUN}")
    public void scheduledTraining() {
        log.info("Scheduled model training triggered");
        trainModel();
    }

    public void trainModel() {
        if (!mlConfig.getAnomalyDetection().isEnabled()) {
            log.info("ML anomaly detection is disabled, skipping training");
            return;
        }

        try {
            log.info("Starting Isolation Forest model training...");

            int trainingDays = mlConfig.getAnomalyDetection().getTrainingDays();
            LocalDateTime since = LocalDateTime.now().minusDays(trainingDays);

            var featureVectors = featureExtractionService.extractFeatureVectorsForTraining(since);

            if (featureVectors.size() < 10) {
                log.warn("Insufficient training data ({} samples), need at least 10",
                        featureVectors.size());

                generateSyntheticTrainingData();

                featureVectors = featureExtractionService.extractFeatureVectorsForTraining(since);
            }

            if (featureVectors.size() >= 10) {
                modelService.train(featureVectors);
                log.info("Model training completed successfully with {} samples", featureVectors.size());
            } else {
                log.error("Still insufficient training data after synthetic generation");
            }

        } catch (Exception e) {
            log.error("Model training failed: {}", e.getMessage(), e);
        }
    }

    private void generateSyntheticTrainingData() {
        log.info("Generating synthetic training data for initial model...");

        double[][] syntheticData = new double[100][];

        for (int i = 0; i < 100; i++) {
            double[] sample = new double[12];

            sample[0] = (6 + (i % 14)) / 23.0;
            sample[1] = ((i % 7) + 1) / 7.0;
            sample[2] = i % 5 / 5.0;
            sample[3] = (i % 10) / 10.0;
            sample[4] = (i % 3 == 0) ? 1.0 : 0.0;
            sample[5] = (i % 24 * 7) / (24.0 * 30);
            sample[6] = (i % 6) / 6.0;
            sample[7] = Math.min(1.0, (i % 20) / 20.0);
            sample[8] = Math.min(1.0, (i % 5) / 20.0);
            sample[9] = Math.min(1.0, (i % 10) / 100.0);
            sample[10] = (i % 5 == 0) ? 1.0 : 0.0;
            sample[11] = (i % 4 == 0) ? 1.0 : 0.0;

            syntheticData[i] = sample;
        }

        modelService.train(java.util.Arrays.asList(syntheticData));
        log.info("Synthetic training data generated and model trained");
    }

    public void triggerManualRetraining() {
        log.info("Manual model retraining triggered");
        trainModel();
    }

    public boolean isModelReady() {
        return modelService.isModelReady();
    }

    public long getLastTrainingTime() {
        return modelService.getLastTrainingTime();
    }
}
