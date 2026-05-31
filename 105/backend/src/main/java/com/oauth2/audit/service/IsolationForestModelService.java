package com.oauth2.audit.service;

import com.oauth2.audit.config.MLConfig;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

import jakarta.annotation.PostConstruct;
import java.io.*;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

@Service
@RequiredArgsConstructor
@Slf4j
public class IsolationForestModelService {
    private final MLConfig mlConfig;

    private static final int NUM_TREES = 100;
    private static final int SAMPLE_SIZE = 256;
    private static final double DEFAULT_CONTAMINATION = 0.1;

    private final List<TreeNode> forest = new ArrayList<>();
    private final Map<String, Double> featureRanges = new ConcurrentHashMap<>();
    private volatile boolean modelReady = false;
    private volatile long lastTrainingTime = 0;

    @PostConstruct
    public void init() {
        log.info("Isolation Forest model service initialized with threshold: {}", mlConfig.getAnomalyDetection().getThreshold());
    }

    public void train(List<double[]> trainingData) {
        if (trainingData == null || trainingData.size() < 10) {
            log.warn("Insufficient training data for model training");
            return;
        }

        log.info("Starting Isolation Forest training with {} samples", trainingData.size());

        calculateFeatureRanges(trainingData);

        List<double[]> normalizedData = normalizeData(trainingData);

        List<double[]> sampleData = sampleIfNeeded(normalizedData);

        forest.clear();
        Random random = new Random(42);

        for (int i = 0; i < NUM_TREES; i++) {
            List<double[]> bootstrapSample = bootstrapSample(sampleData, SAMPLE_SIZE, random);
            TreeNode root = buildTree(bootstrapSample, 0, getMaxDepth(bootstrapSample), random);
            forest.add(root);
        }

        modelReady = true;
        lastTrainingTime = System.currentTimeMillis();

        log.info("Isolation Forest training completed. Forest size: {}, Last training time: {}",
                forest.size(), new Date(lastTrainingTime)));
    }

    public double predictAnomalyScore(double[] features) {
        if (!modelReady) {
            log.warn("Model not ready for prediction");
            return 0.0;
        }

        double[] normalizedFeatures = normalizeFeatures(features);

        double[] pathLengths = new double[forest.size()];
        for (int i = 0; i < forest.size(); i++) {
            pathLengths[i] = computePathLength(normalizedFeatures, forest.get(i), 0);
        }

        double avgPathLength = Arrays.stream(pathLengths).average().orElse(0.0);
        double score = Math.pow(2, -avgPathLength / averagePathLength(SAMPLE_SIZE));

        return Math.min(1.0, Math.max(0.0, score));
    }

    public boolean isAnomaly(double[] features) {
        double score = predictAnomalyScore(features);
        return score >= mlConfig.getAnomalyDetection().getThreshold();
    }

    public boolean isModelReady() {
        return modelReady;
    }

    public long getLastTrainingTime() {
        return lastTrainingTime;
    }

    private void calculateFeatureRanges(List<double[]> data) {
        if (data.isEmpty()) return;

        int numFeatures = data.get(0).length;
        double[] minValues = new double[numFeatures];
        double[] maxValues = new double[numFeatures];

        Arrays.fill(minValues, Double.MAX_VALUE);
        Arrays.fill(maxValues, Double.MIN_VALUE);

        for (double[] sample : data) {
            for (int i = 0; i < numFeatures; i++) {
                minValues[i] = Math.min(minValues[i], sample[i]);
                maxValues[i] = Math.max(maxValues[i], sample[i]);
            }
        }

        featureRanges.clear();
        for (int i = 0; i < numFeatures; i++) {
            featureRanges.put("min_" + i, minValues[i]);
            featureRanges.put("max_" + i, maxValues[i]);
        }
    }

    private List<double[]> normalizeData(List<double[]> data) {
        List<double[]> normalized = new ArrayList<>();
        for (double[] sample : data) {
            normalized.add(normalizeFeatures(sample));
        }
        return normalized;
    }

    private double[] normalizeFeatures(double[] features) {
        double[] normalized = new double[features.length];
        for (int i = 0; i < features.length; i++) {
            double min = featureRanges.getOrDefault("min_" + i, 0.0);
            double max = featureRanges.getOrDefault("max_" + i, 1.0);
            if (max - min > 0) {
                normalized[i] = (features[i] - min) / (max - min);
            } else {
                normalized[i] = 0.5;
            }
        }
        return normalized;
    }

    private List<double[]> sampleIfNeeded(List<double[]> data) {
        if (data.size() <= SAMPLE_SIZE) {
            return data;
        }
        List<double[]> sample = new ArrayList<>(SAMPLE_SIZE);
        List<Integer> indices = new ArrayList<>();
        for (int i = 0; i < data.size(); i++) indices.add(i);
        Collections.shuffle(indices);
        for (int i = 0; i < SAMPLE_SIZE; i++) {
            sample.add(data.get(indices.get(i)));
        }
        return sample;
    }

    private List<double[]> bootstrapSample(List<double[]> data, int size, Random random) {
        List<double[]> sample = new ArrayList<>(size);
        for (int i = 0; i < size; i++) {
            sample.add(data.get(random.nextInt(data.size())));
        }
        return sample;
    }

    private int getMaxDepth(List<double[]> data) {
        return (int) Math.ceil(Math.log(data.size()) / Math.log(2));
    }

    private TreeNode buildTree(List<double[]> data, int depth, int maxDepth, Random random) {
        if (data.isEmpty() || depth >= maxDepth || data.size() <= 1) {
            return new TreeNode(null, null, null, true, 0);
        }

        int numFeatures = data.get(0).length;
        int featureIdx = random.nextInt(numFeatures);

        double minVal = Double.MAX_VALUE;
        double maxVal = Double.MIN_VALUE;
        for (double[] sample : data) {
            minVal = Math.min(minVal, sample[featureIdx]);
            maxVal = Math.max(maxVal, sample[featureIdx]);
        }

        if (Math.abs(maxVal - minVal) < 1e-10) {
            return new TreeNode(null, null, null, true, 0);
        }

        double splitValue = minVal + random.nextDouble() * (maxVal - minVal);

        List<double[]> leftData = new ArrayList<>();
        List<double[]> rightData = new ArrayList<>();
        for (double[] sample : data) {
            if (sample[featureIdx] < splitValue) {
                leftData.add(sample);
            } else {
                rightData.add(sample);
            }
        }

        TreeNode left = buildTree(leftData, depth + 1, maxDepth, random);
        TreeNode right = buildTree(rightData, depth + 1, maxDepth, random);

        return new TreeNode(left, right, splitValue, false, featureIdx);
    }

    private double computePathLength(double[] features, TreeNode node, int depth) {
        if (node.isExternal || depth > 100) {
            return depth + averagePathLength(SAMPLE_SIZE);
        }

        double splitValue = node.splitValue;
        int featureIdx = node.featureIndex;

        if (features[featureIdx] < splitValue) {
            return computePathLength(features, node.left, depth + 1);
        } else {
            return computePathLength(features, node.right, depth + 1);
        }
    }

    private double averagePathLength(int sampleSize) {
        if (sampleSize <= 1) return 0;
        return 2.0 * (Math.log(sampleSize - 1) + 0.5772156649) - (2.0 * (sampleSize - 1) / sampleSize);
    }

    public void saveModel(String path) throws IOException {
        try (ObjectOutputStream oos = new ObjectOutputStream(new FileOutputStream(path))) {
            oos.writeObject(new ArrayList<>(forest));
            oos.writeObject(new HashMap<>(featureRanges));
            oos.writeLong(lastTrainingTime);
        }
    }

    @SuppressWarnings("unchecked")
    public void loadModel(String path) throws IOException, ClassNotFoundException {
        try (ObjectInputStream ois = new ObjectInputStream(new FileInputStream(path))) {
            List<TreeNode> loadedForest = (List<TreeNode>) ois.readObject();
            Map<String, Double> loadedRanges = (Map<String, Double>) ois.readObject();
            long loadedTime = ois.readLong();

            forest.clear();
            forest.addAll(loadedForest);
            featureRanges.clear();
            featureRanges.putAll(loadedRanges);
            lastTrainingTime = loadedTime;
            modelReady = true;

            log.info("Model loaded from {}", path);
        }
    }

    private static class TreeNode implements Serializable {
        final TreeNode left;
        final TreeNode right;
        final Double splitValue;
        final boolean isExternal;
        final int featureIndex;

        TreeNode(TreeNode left, TreeNode right, Double splitValue, boolean isExternal, int featureIndex) {
            this.left = left;
            this.right = right;
            this.splitValue = splitValue;
            this.isExternal = isExternal;
            this.featureIndex = featureIndex;
        }
    }
}
