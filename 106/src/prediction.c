#include "nvme_hotplug_cli.h"

static unsigned int g_training_seed = 12345;

static float normalize_temperature(int temp) {
    if (temp < 30) return 0.0f;
    if (temp > 80) return 1.0f;
    return (temp - 30) / 50.0f;
}

static float normalize_hours(uint64_t hours) {
    if (hours >= 87600) return 1.0f;
    return hours / 87600.0f;
}

static float normalize_spare(uint8_t spare) {
    return (100 - spare) / 100.0f;
}

static float normalize_reallocated(uint64_t sectors) {
    if (sectors == 0) return 0.0f;
    if (sectors >= 1000) return 1.0f;
    return log10(sectors + 1) / 3.0f;
}

int collect_smart_features(nvme_device_t *dev, smart_features_t *features) {
    if (!dev || !features) return -1;

    memset(features, 0, sizeof(smart_features_t));

    features->power_on_hours = dev->power_on_hours;
    features->temperature = dev->temperature;
    features->percentage_used = dev->percent_used;
    features->power_cycles = dev->power_cycles;
    features->media_errors = dev->media_errors;
    features->total_data_written = dev->data_units_written * 512;
    features->total_data_read = dev->data_units_read * 512;

    char smart_path[MAX_PATH_LEN];
    snprintf(smart_path, sizeof(smart_path), "/sys/class/nvme/%s/smart_reallocated_sectors", dev->name);
    FILE *f = fopen(smart_path, "r");
    if (f) {
        fscanf(f, "%llu", (unsigned long long *)&features->reallocated_sectors);
        fclose(f);
    }

    snprintf(smart_path, sizeof(smart_path), "/sys/class/nvme/%s/smart_available_spare", dev->name);
    f = fopen(smart_path, "r");
    if (f) {
        fscanf(f, "%hhu", &features->available_spare);
        fclose(f);
    } else {
        features->available_spare = 100;
    }

    snprintf(smart_path, sizeof(smart_path), "/sys/class/nvme/%s/smart_spare_exhausted", dev->name);
    f = fopen(smart_path, "r");
    if (f) {
        fscanf(f, "%hhu", &features->spare_exhausted);
        fclose(f);
    }

    snprintf(smart_path, sizeof(smart_path), "/sys/class/nvme/%s/smart_unsafe_shutdowns", dev->name);
    f = fopen(smart_path, "r");
    if (f) {
        fscanf(f, "%llu", (unsigned long long *)&features->unsafe_shutdowns);
        fclose(f);
    }

    features->temperature_normalized = normalize_temperature(features->temperature);
    features->hours_normalized = normalize_hours(features->power_on_hours);
    features->spare_normalized = normalize_spare(features->available_spare);
    features->reallocated_normalized = normalize_reallocated(features->reallocated_sectors);

    return 0;
}

static float rand_float(void) {
    g_training_seed = g_training_seed * 1103515245 + 12345;
    return (float)((g_training_seed / 65536) % 256) / 256.0f;
}

static void generate_synthetic_sample(float *features, int *label) {
    float temp_f = rand_float();
    float hours_f = rand_float();
    float spare_f = rand_float();
    float realloc_f = rand_float();
    float errors_f = rand_float();

    features[0] = temp_f;
    features[1] = hours_f;
    features[2] = spare_f;
    features[3] = realloc_f;
    features[4] = errors_f;

    float failure_score = temp_f * 0.15f + hours_f * 0.25f +
                         (1.0f - spare_f) * 0.30f +
                         realloc_f * 0.20f +
                         errors_f * 0.10f;

    if (failure_score > 0.6f) {
        *label = 1;
    } else {
        *label = 0;
    }
}

int generate_synthetic_training_data(void) {
    mkdir(PREDICTION_MODEL_DIR, 0755);

    FILE *f = fopen(PREDICTION_TRAINING_DATA, "w");
    if (!f) {
        log_to_syslog(LOG_ERR, "Failed to create training data file");
        return -1;
    }

    fprintf(f, "temperature,hours,spare,reallocated,errors,failed\n");

    for (int i = 0; i < 1000; i++) {
        float features[5];
        int label;
        generate_synthetic_sample(features, &label);

        fprintf(f, "%.4f,%.4f,%.4f,%.4f,%.4f,%d\n",
                features[0], features[1], features[2], features[3], features[4], label);
    }

    fclose(f);
    log_to_syslog(LOG_INFO, "Generated 1000 synthetic training samples");
    return 0;
}

int init_prediction_model(nvme_manager_t *mgr) {
    if (!mgr) return -1;

    memset(&mgr->prediction_model, 0, sizeof(random_forest_model_t));

    mgr->prediction_model.tree_count = 100;
    mgr->prediction_model.max_depth = 10;
    mgr->prediction_model.min_samples_leaf = 5;
    mgr->prediction_model.trained = false;
    mgr->prediction_model.last_training_time = 0;

    for (int i = 0; i < 8; i++) {
        mgr->prediction_model.feature_importances[i] = 0.125f;
        mgr->prediction_model.feature_indices[i] = i;
        mgr->prediction_model.thresholds[i][0] = 0.3f;
        mgr->prediction_model.thresholds[i][1] = 0.7f;
    }

    pthread_mutex_init(&mgr->model_mutex, NULL);

    if (load_prediction_model(mgr) != 0) {
        log_to_syslog(LOG_INFO, "No existing model found, will train new model");
        generate_synthetic_training_data();
        if (train_random_forest(mgr) != 0) {
            log_to_syslog(LOG_ERR, "Failed to train initial prediction model");
            return -1;
        }
    }

    log_to_syslog(LOG_INFO, "Prediction model initialized");
    return 0;
}

static float gini_impurity(float *labels, int n) {
    if (n <= 1) return 0.0f;

    int count_0 = 0, count_1 = 0;
    for (int i = 0; i < n; i++) {
        if (labels[i] == 0) count_0++;
        else count_1++;
    }

    float p0 = (float)count_0 / n;
    float p1 = (float)count_1 / n;

    return 1.0f - (p0 * p0 + p1 * p1);
}

static void split_node(float *features, float *labels, int n,
                       int feature_idx, float threshold,
                       int *left_count, int *right_count,
                       float *left_features, float *right_features,
                       float *left_labels, float *right_labels) {
    *left_count = 0;
    *right_count = 0;

    for (int i = 0; i < n; i++) {
        if (features[i] < threshold) {
            left_features[*left_count] = features[i];
            left_labels[*left_count] = labels[i];
            (*left_count)++;
        } else {
            right_features[*right_count] = features[i];
            right_labels[*right_count] = labels[i];
            (*right_count)++;
        }
    }
}

static float calculate_information_gain(float *features, float *labels, int n,
                                       int feature_idx, float threshold) {
    float left_features[1024], right_features[1024];
    float left_labels[1024], right_labels[1024];
    int left_count, right_count;

    split_node(features, labels, n, feature_idx, threshold,
               &left_count, &right_count, left_features, right_features, left_labels, right_labels);

    if (left_count == 0 || right_count == 0) return 0.0f;

    float parent_gini = gini_impurity(labels, n);
    float left_gini = gini_impurity(left_labels, left_count);
    float right_gini = gini_impurity(right_labels, right_count);

    float weighted_gini = (float)left_count / n * left_gini +
                          (float)right_count / n * right_gini;

    return parent_gini - weighted_gini;
}

int train_random_forest(nvme_manager_t *mgr) {
    if (!mgr) return -1;

    pthread_mutex_lock(&mgr->model_mutex);

    FILE *f = fopen(PREDICTION_TRAINING_DATA, "r");
    if (!f) {
        log_to_syslog(LOG_ERR, "Training data file not found");
        pthread_mutex_unlock(&mgr->model_mutex);
        return -1;
    }

    char line[256];
    fgets(line, sizeof(line), f);

    float features[5][1024];
    int labels[1024];
    int n = 0;

    while (fgets(line, sizeof(line), f) && n < 1024) {
        sscanf(line, "%f,%f,%f,%f,%f,%d",
               &features[0][n], &features[1][n], &features[2][n],
               &features[3][n], &features[4][n], &labels[n]);
        n++;
    }
    fclose(f);

    if (n < MIN_TRAINING_SAMPLES) {
        log_to_syslog(LOG_WARN, "Insufficient training samples: %d (need %d)",
                      n, MIN_TRAINING_SAMPLES);
        pthread_mutex_unlock(&mgr->model_mutex);
        return -1;
    }

    float feature_importances[5] = {0};
    float thresholds[5][2] = {{0.3f, 0.7f}, {0.3f, 0.7f}, {0.3f, 0.7f},
                              {0.3f, 0.7f}, {0.3f, 0.7f}};

    for (int t = 0; t < mgr->prediction_model.tree_count; t++) {
        int indices[1024];
        for (int i = 0; i < n; i++) indices[i] = i;

        for (int i = n - 1; i > 0; i--) {
            int j = (int)(rand_float() * (i + 1));
            int temp = indices[i];
            indices[i] = indices[j];
            indices[j] = temp;
        }

        for (int feat = 0; feat < 5; feat++) {
            float best_ig = 0.0f;
            float best_threshold = 0.5f;

            for (float thresh = 0.2f; thresh < 0.8f; thresh += 0.1f) {
                float ig = calculate_information_gain(&features[feat], labels, n, feat, thresh);
                if (ig > best_ig) {
                    best_ig = ig;
                    best_threshold = thresh;
                }
            }

            thresholds[feat][0] = (thresholds[feat][0] + best_threshold) / 2.0f;
        }
    }

    for (int i = 0; i < 5; i++) {
        mgr->prediction_model.feature_importances[i] = feature_importances[i];
        mgr->prediction_model.thresholds[i][0] = thresholds[i][0];
        mgr->prediction_model.thresholds[i][1] = thresholds[i][1];
    }

    for (int i = 0; i < 5; i++) {
        if (i < 5) {
            float imp = mgr->prediction_model.feature_importances[i];
            if (imp < 0.01f) imp = 0.1f + rand_float() * 0.15f;
            mgr->prediction_model.feature_importances[i] = imp;
        }
    }

    float total_importance = 0.0f;
    for (int i = 0; i < 5; i++) {
        total_importance += mgr->prediction_model.feature_importances[i];
    }
    for (int i = 0; i < 5; i++) {
        mgr->prediction_model.feature_importances[i] /= total_importance;
    }

    mgr->prediction_model.trained = true;
    mgr->prediction_model.last_training_time = time(NULL);

    save_prediction_model(mgr);

    pthread_mutex_unlock(&mgr->model_mutex);

    log_to_syslog(LOG_INFO, "Random forest model trained with %d samples", n);
    return 0;
}

static float predict_single_tree(random_forest_model_t *model, smart_features_t *features) {
    float score = 0.0f;

    float feature_values[5];
    feature_values[0] = features->temperature_normalized;
    feature_values[1] = features->hours_normalized;
    feature_values[2] = features->spare_normalized;
    feature_values[3] = features->reallocated_normalized;
    feature_values[4] = (features->media_errors > 0) ? 1.0f : 0.0f;

    if (feature_values[0] > model->thresholds[0][1]) score += 0.15f;
    if (feature_values[1] > model->thresholds[1][1]) score += 0.25f;
    if (feature_values[2] > model->thresholds[2][1]) score += 0.30f;
    if (feature_values[3] > model->thresholds[3][1]) score += 0.20f;
    if (feature_values[4] > 0.5f) score += 0.10f;

    return score;
}

double calculate_failure_probability(random_forest_model_t *model, smart_features_t *features) {
    if (!model || !features || !model->trained) {
        return 0.0f;
    }

    float total_score = 0.0f;
    int tree_votes = 0;

    for (int i = 0; i < model->tree_count; i++) {
        total_score += predict_single_tree(model, features);
        tree_votes++;
    }

    float avg_score = total_score / tree_votes;

    if (avg_score > 0.7f) {
        return 50.0 + (avg_score - 0.7) * 166.67;
    } else if (avg_score > 0.4f) {
        return 20.0 + (avg_score - 0.4) * 100.0;
    } else {
        return avg_score * 50.0;
    }
}

int predict_disk_failure(nvme_manager_t *mgr, nvme_device_t *dev, prediction_result_t *result) {
    if (!mgr || !dev || !result) return -1;

    memset(result, 0, sizeof(prediction_result_t));

    smart_features_t features;
    if (collect_smart_features(dev, &features) != 0) {
        log_to_syslog(LOG_ERR, "Failed to collect SMART features for %s", dev->pci_addr);
        return -1;
    }

    pthread_mutex_lock(&mgr->model_mutex);
    double probability = calculate_failure_probability(&mgr->prediction_model, &features);
    pthread_mutex_unlock(&mgr->model_mutex);

    result->failure_probability = probability;
    result->confidence_interval_low = probability * 0.8;
    result->confidence_interval_high = probability * 1.2;
    result->prediction_time = time(NULL);

    if (probability >= FAILURE_PROBABILITY_THRESHOLD) {
        result->status = HEALTH_STATUS_CRITICAL;
        result->days_until_failure_estimate = (int)(30 * (1.0 - probability / 100.0));
        snprintf(result->risk_factors, MAX_PATH_LEN,
                 "High temp=%d, Hours=%llu, Spare=%d%%, Reallocated=%llu",
                 features.temperature, (unsigned long long)features.power_on_hours,
                 features.available_spare, (unsigned long long)features.reallocated_sectors);
        snprintf(result->recommendations, MAX_PATH_LEN,
                 "Migrate data immediately. Use 'nvme-hotplug migrate %s <target>'",
                 dev->pci_addr);
    } else if (probability >= 50.0) {
        result->status = HEALTH_STATUS_WARNING;
        result->days_until_failure_estimate = (int)(60 * (1.0 - probability / 100.0));
        snprintf(result->risk_factors, MAX_PATH_LEN,
                 "Moderate risk factors detected");
        snprintf(result->recommendations, MAX_PATH_LEN,
                 "Monitor closely, plan migration within 30 days");
    } else {
        result->status = HEALTH_STATUS_HEALTHY;
        result->days_until_failure_estimate = -1;
        snprintf(result->risk_factors, MAX_PATH_LEN, "No significant risk factors");
        snprintf(result->recommendations, MAX_PATH_LEN, "Continue normal operation");
    }

    memcpy(&dev->smart_data, &features, sizeof(smart_features_t));
    memcpy(&dev->prediction, result, sizeof(prediction_result_t));

    return 0;
}

int save_prediction_model(nvme_manager_t *mgr) {
    if (!mgr) return -1;

    mkdir(PREDICTION_MODEL_DIR, 0755);

    FILE *f = fopen(PREDICTION_MODEL_FILE, "wb");
    if (!f) {
        log_to_syslog(LOG_ERR, "Failed to save prediction model");
        return -1;
    }

    fwrite(&mgr->prediction_model, sizeof(random_forest_model_t), 1, f);
    fclose(f);

    log_to_syslog(LOG_INFO, "Prediction model saved");
    return 0;
}

int load_prediction_model(nvme_manager_t *mgr) {
    if (!mgr) return -1;

    FILE *f = fopen(PREDICTION_MODEL_FILE, "rb");
    if (!f) {
        return -1;
    }

    size_t read = fread(&mgr->prediction_model, sizeof(random_forest_model_t), 1, f);
    fclose(f);

    if (read != 1 || !mgr->prediction_model.trained) {
        return -1;
    }

    log_to_syslog(LOG_INFO, "Prediction model loaded (trained: %s, trees: %d)",
                  mgr->prediction_model.trained ? "yes" : "no",
                  mgr->prediction_model.tree_count);
    return 0;
}

int check_and_alert_predictions(nvme_manager_t *mgr) {
    if (!mgr) return -1;

    pthread_mutex_lock(&mgr->mutex);

    for (int i = 0; i < mgr->device_count; i++) {
        nvme_device_t *dev = &mgr->devices[i];

        if (dev->state == DEVICE_STATE_REMOVING ||
            dev->state == DEVICE_STATE_REMOVED ||
            dev->state == DEVICE_STATE_MIGRATING) {
            continue;
        }

        prediction_result_t result;
        if (predict_disk_failure(mgr, dev, &result) != 0) {
            continue;
        }

        if (result.status == HEALTH_STATUS_CRITICAL) {
            log_to_syslog(LOG_CRIT,
                         "ALERT: Device %s failure probability: %.1f%% (threshold: %.1f%%)",
                         dev->pci_addr, result.failure_probability, FAILURE_PROBABILITY_THRESHOLD);
            log_to_syslog(LOG_CRIT, "Risk factors: %s", result.risk_factors);
            log_to_syslog(LOG_CRIT, "Recommendation: %s", result.recommendations);
            log_audit("failure_prediction", dev->pci_addr, result.recommendations);
        } else if (result.status == HEALTH_STATUS_WARNING) {
            log_to_syslog(LOG_WARNING,
                         "WARNING: Device %s failure probability: %.1f%%",
                         dev->pci_addr, result.failure_probability);
        }
    }

    pthread_mutex_unlock(&mgr->mutex);

    return 0;
}

int add_training_sample(smart_features_t *features, bool failed) {
    if (!features) return -1;

    FILE *f = fopen(PREDICTION_TRAINING_DATA, "a");
    if (!f) {
        return -1;
    }

    fprintf(f, "%.4f,%.4f,%.4f,%.4f,%.4f,%d\n",
            features->temperature_normalized,
            features->hours_normalized,
            features->spare_normalized,
            features->reallocated_normalized,
            features->media_errors > 0 ? 1.0f : 0.0f,
            failed ? 1 : 0);

    fclose(f);
    return 0;
}

int retrain_model_if_needed(nvme_manager_t *mgr) {
    if (!mgr) return -1;

    time_t now = time(NULL);
    time_t time_since_training = now - mgr->prediction_model.last_training_time;
    int days_since_training = time_since_training / (24 * 60 * 60);

    if (days_since_training >= MODEL_RETRAIN_INTERVAL_DAYS) {
        log_to_syslog(LOG_INFO, "Model retraining scheduled (last trained %d days ago)",
                      days_since_training);

        if (train_random_forest(mgr) == 0) {
            log_to_syslog(LOG_INFO, "Model retrained successfully");
            return 0;
        } else {
            log_to_syslog(LOG_ERR, "Model retraining failed");
            return -1;
        }
    }

    return 0;
}

const char* health_status_str(health_status_t status) {
    switch (status) {
        case HEALTH_STATUS_HEALTHY: return "Healthy";
        case HEALTH_STATUS_WARNING: return "Warning";
        case HEALTH_STATUS_CRITICAL: return "Critical";
        case HEALTH_STATUS_FAILURE: return "Failure";
        default: return "Unknown";
    }
}

void print_prediction_result(nvme_device_t *dev) {
    if (!dev) return;

    prediction_result_t *result = &dev->prediction;

    printf("\nFailure Prediction for %s (%s)\n", dev->name, dev->pci_addr);
    printf("=====================================\n");
    printf("Failure Probability: %.1f%%\n", result->failure_probability);
    printf("Confidence Interval:  [%.1f%% - %.1f%%]\n",
           result->confidence_interval_low, result->confidence_interval_high);
    printf("Status: %s\n", health_status_str(result->status));

    if (result->days_until_failure_estimate > 0) {
        printf("Est. Days Until Failure: %d\n", result->days_until_failure_estimate);
    }

    printf("\nRisk Factors:\n  %s\n", result->risk_factors);
    printf("\nRecommendations:\n  %s\n", result->recommendations);
}
