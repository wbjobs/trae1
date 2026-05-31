#include "behavior_learning.h"
#include <math.h>

static double calculate_stddev(double *values, int count, double mean) {
    if (count <= 1) return 0.0;
    
    double sum_sq = 0.0;
    for (int i = 0; i < count; i++) {
        double diff = values[i] - mean;
        sum_sq += diff * diff;
    }
    return sqrt(sum_sq / (count - 1));
}

static UserBehaviorProfile *get_or_create_profile(BehaviorLearner *learner, const char *username) {
    for (int i = 0; i < learner->profile_count; i++) {
        if (strcmp(learner->profiles[i].username, username) == 0) {
            return &learner->profiles[i];
        }
    }
    
    if (learner->profile_count >= MAX_SESSIONS) {
        return NULL;
    }
    
    UserBehaviorProfile *profile = &learner->profiles[learner->profile_count];
    memset(profile, 0, sizeof(UserBehaviorProfile));
    strncpy(profile->username, username, MAX_USERNAME - 1);
    profile->current_hour_start = time(NULL);
    
    if (pthread_mutex_init(&profile->lock, NULL) != 0) {
        return NULL;
    }
    
    learner->profile_count++;
    return profile;
}

int behavior_learner_init(BehaviorLearner *learner, const char *data_dir) {
    memset(learner, 0, sizeof(BehaviorLearner));
    learner->startup_time = time(NULL);
    
    if (pthread_mutex_init(&learner->lock, NULL) != 0) {
        return -1;
    }
    
    char baseline_path[MAX_PATH_LENGTH];
    snprintf(baseline_path, sizeof(baseline_path), "%s/behavior_baselines.dat", 
             data_dir && strlen(data_dir) > 0 ? data_dir : ".");
    
    learner->baseline_file = fopen(baseline_path, "a+");
    if (!learner->baseline_file) {
        learner->baseline_file = fopen("behavior_baselines.dat", "a+");
    }
    
    return 0;
}

int behavior_learner_add_sample(BehaviorLearner *learner, const char *username, 
                                 const BehaviorSample *sample) {
    pthread_mutex_lock(&learner->lock);
    
    UserBehaviorProfile *profile = get_or_create_profile(learner, username);
    if (!profile) {
        pthread_mutex_unlock(&learner->lock);
        return -1;
    }
    
    pthread_mutex_lock(&profile->lock);
    
    if (profile->sample_count < BEHAVIOR_MAX_SAMPLES) {
        memcpy(&profile->samples[profile->sample_count], sample, sizeof(BehaviorSample));
        profile->sample_count++;
    } else {
        memmove(&profile->samples[0], &profile->samples[1], 
                (BEHAVIOR_MAX_SAMPLES - 1) * sizeof(BehaviorSample));
        memcpy(&profile->samples[BEHAVIOR_MAX_SAMPLES - 1], sample, sizeof(BehaviorSample));
    }
    
    profile->baseline.last_update = time(NULL);
    
    pthread_mutex_unlock(&profile->lock);
    pthread_mutex_unlock(&learner->lock);
    
    return 0;
}

int behavior_learner_update_current(BehaviorLearner *learner, const char *username,
                                     double read_iops, double write_iops,
                                     double avg_entropy, double max_entropy,
                                     size_t bytes_written, size_t files_created,
                                     size_t files_modified, size_t files_deleted,
                                     size_t files_renamed) {
    pthread_mutex_lock(&learner->lock);
    
    UserBehaviorProfile *profile = get_or_create_profile(learner, username);
    if (!profile) {
        pthread_mutex_unlock(&learner->lock);
        return -1;
    }
    
    pthread_mutex_lock(&profile->lock);
    
    time_t now = time(NULL);
    if (now - profile->current_hour_start >= BEHAVIOR_SAMPLE_INTERVAL) {
        profile->current_hour.timestamp = profile->current_hour_start;
        profile->current_hour.write_ratio = (profile->current_hour.write_iops + 
                                            profile->current_hour.read_iops) > 0 ?
            (double)profile->current_hour.write_iops / 
            (profile->current_hour.write_iops + profile->current_hour.read_iops) : 0;
        
        behavior_learner_add_sample(learner, username, &profile->current_hour);
        
        memset(&profile->current_hour, 0, sizeof(BehaviorSample));
        profile->current_hour_start = now;
        
        if (profile->sample_count >= 168 && !profile->baseline.baseline_established) {
            behavior_learner_establish_baseline(learner, username);
        }
    }
    
    profile->current_hour.read_iops += read_iops;
    profile->current_hour.write_iops += write_iops;
    profile->current_hour.avg_entropy = avg_entropy;
    profile->current_hour.max_entropy = max_entropy;
    profile->current_hour.bytes_written += bytes_written;
    profile->current_hour.files_created += files_created;
    profile->current_hour.files_modified += files_modified;
    profile->current_hour.files_deleted += files_deleted;
    profile->current_hour.files_renamed += files_renamed;
    
    pthread_mutex_unlock(&profile->lock);
    pthread_mutex_unlock(&learner->lock);
    
    return 0;
}

int behavior_learner_establish_baseline(BehaviorLearner *learner, const char *username) {
    pthread_mutex_lock(&learner->lock);
    
    UserBehaviorProfile *profile = NULL;
    for (int i = 0; i < learner->profile_count; i++) {
        if (strcmp(learner->profiles[i].username, username) == 0) {
            profile = &learner->profiles[i];
            break;
        }
    }
    
    if (!profile || profile->sample_count < 24) {
        pthread_mutex_unlock(&learner->lock);
        return -1;
    }
    
    pthread_mutex_lock(&profile->lock);
    
    double *read_iops = (double *)malloc(profile->sample_count * sizeof(double));
    double *write_iops = (double *)malloc(profile->sample_count * sizeof(double));
    double *avg_entropy = (double *)malloc(profile->sample_count * sizeof(double));
    double *max_entropy = (double *)malloc(profile->sample_count * sizeof(double));
    double *write_ratio = (double *)malloc(profile->sample_count * sizeof(double));
    
    if (!read_iops || !write_iops || !avg_entropy || !max_entropy || !write_ratio) {
        free(read_iops);
        free(write_iops);
        free(avg_entropy);
        free(max_entropy);
        free(write_ratio);
        pthread_mutex_unlock(&profile->lock);
        pthread_mutex_unlock(&learner->lock);
        return -1;
    }
    
    double sum_read = 0, sum_write = 0, sum_avg_ent = 0, sum_max_ent = 0, sum_ratio = 0;
    
    for (int i = 0; i < profile->sample_count; i++) {
        read_iops[i] = profile->samples[i].read_iops;
        write_iops[i] = profile->samples[i].write_iops;
        avg_entropy[i] = profile->samples[i].avg_entropy;
        max_entropy[i] = profile->samples[i].max_entropy;
        write_ratio[i] = profile->samples[i].write_ratio;
        
        sum_read += read_iops[i];
        sum_write += write_iops[i];
        sum_avg_ent += avg_entropy[i];
        sum_max_ent += max_entropy[i];
        sum_ratio += write_ratio[i];
    }
    
    int n = profile->sample_count;
    
    profile->baseline.mean_read_iops = sum_read / n;
    profile->baseline.mean_write_iops = sum_write / n;
    profile->baseline.mean_avg_entropy = sum_avg_ent / n;
    profile->baseline.mean_max_entropy = sum_max_ent / n;
    profile->baseline.mean_write_ratio = sum_ratio / n;
    
    profile->baseline.std_read_iops = calculate_stddev(read_iops, n, profile->baseline.mean_read_iops);
    profile->baseline.std_write_iops = calculate_stddev(write_iops, n, profile->baseline.mean_write_iops);
    profile->baseline.std_avg_entropy = calculate_stddev(avg_entropy, n, profile->baseline.mean_avg_entropy);
    profile->baseline.std_max_entropy = calculate_stddev(max_entropy, n, profile->baseline.mean_max_entropy);
    profile->baseline.std_write_ratio = calculate_stddev(write_ratio, n, profile->baseline.mean_write_ratio);
    
    profile->baseline.baseline_established = true;
    profile->baseline.sample_count = n;
    profile->baseline.last_update = time(NULL);
    
    free(read_iops);
    free(write_iops);
    free(avg_entropy);
    free(max_entropy);
    free(write_ratio);
    
    pthread_mutex_unlock(&profile->lock);
    pthread_mutex_unlock(&learner->lock);
    
    printf("Behavior baseline established for user '%s' after %d samples\n", 
           username, n);
    
    return 0;
}

bool behavior_learner_is_anomalous(BehaviorLearner *learner, const char *username,
                                    double read_iops, double write_iops,
                                    double avg_entropy, double max_entropy,
                                    double write_ratio) {
    double score = behavior_learner_get_anomaly_score(learner, username,
                                                      read_iops, write_iops,
                                                      avg_entropy, max_entropy,
                                                      write_ratio);
    return score > BEHAVIOR_ANOMALY_FACTOR;
}

double behavior_learner_get_anomaly_score(BehaviorLearner *learner, const char *username,
                                           double read_iops, double write_iops,
                                           double avg_entropy, double max_entropy,
                                           double write_ratio) {
    pthread_mutex_lock(&learner->lock);
    
    UserBehaviorProfile *profile = NULL;
    for (int i = 0; i < learner->profile_count; i++) {
        if (strcmp(learner->profiles[i].username, username) == 0) {
            profile = &learner->profiles[i];
            break;
        }
    }
    
    if (!profile || !profile->baseline.baseline_established) {
        pthread_mutex_unlock(&learner->lock);
        return 0.0;
    }
    
    double score = 0.0;
    int factors = 0;
    
    if (profile->baseline.std_write_iops > 0) {
        double z = fabs(write_iops - profile->baseline.mean_write_iops) / profile->baseline.std_write_iops;
        score += z;
        factors++;
    }
    
    if (profile->baseline.std_max_entropy > 0) {
        double z = fabs(max_entropy - profile->baseline.mean_max_entropy) / profile->baseline.std_max_entropy;
        score += z;
        factors++;
    }
    
    if (profile->baseline.std_write_ratio > 0) {
        double z = fabs(write_ratio - profile->baseline.mean_write_ratio) / profile->baseline.std_write_ratio;
        score += z;
        factors++;
    }
    
    pthread_mutex_unlock(&learner->lock);
    
    return factors > 0 ? score / factors : 0.0;
}

int behavior_learner_save_baselines(BehaviorLearner *learner, const char *path) {
    if (!learner->baseline_file) return -1;
    
    pthread_mutex_lock(&learner->lock);
    
    rewind(learner->baseline_file);
    
    for (int i = 0; i < learner->profile_count; i++) {
        UserBehaviorProfile *p = &learner->profiles[i];
        fprintf(learner->baseline_file, "USER:%s\n", p->username);
        fprintf(learner->baseline_file, "SAMPLES:%d\n", p->sample_count);
        fprintf(learner->baseline_file, "BASELINE:%d\n", p->baseline.baseline_established);
        fprintf(learner->baseline_file, "MEAN_READ:%.4f\n", p->baseline.mean_read_iops);
        fprintf(learner->baseline_file, "STD_READ:%.4f\n", p->baseline.std_read_iops);
        fprintf(learner->baseline_file, "MEAN_WRITE:%.4f\n", p->baseline.mean_write_iops);
        fprintf(learner->baseline_file, "STD_WRITE:%.4f\n", p->baseline.std_write_iops);
        fprintf(learner->baseline_file, "MEAN_ENT:%.4f\n", p->baseline.mean_avg_entropy);
        fprintf(learner->baseline_file, "STD_ENT:%.4f\n", p->baseline.std_avg_entropy);
        fprintf(learner->baseline_file, "MEAN_MAX_ENT:%.4f\n", p->baseline.mean_max_entropy);
        fprintf(learner->baseline_file, "STD_MAX_ENT:%.4f\n", p->baseline.std_max_entropy);
        fprintf(learner->baseline_file, "MEAN_RATIO:%.4f\n", p->baseline.mean_write_ratio);
        fprintf(learner->baseline_file, "STD_RATIO:%.4f\n", p->baseline.std_write_ratio);
        fprintf(learner->baseline_file, "---\n");
    }
    
    fflush(learner->baseline_file);
    pthread_mutex_unlock(&learner->lock);
    
    return 0;
}

void behavior_learner_stats(BehaviorLearner *learner, const char *username) {
    pthread_mutex_lock(&learner->lock);
    
    UserBehaviorProfile *profile = NULL;
    for (int i = 0; i < learner->profile_count; i++) {
        if (strcmp(learner->profiles[i].username, username) == 0) {
            profile = &learner->profiles[i];
            break;
        }
    }
    
    if (!profile) {
        printf("No behavior profile for user: %s\n", username);
        pthread_mutex_unlock(&learner->lock);
        return;
    }
    
    printf("\n=== Behavior Profile for %s ===\n", username);
    printf("Samples: %d\n", profile->sample_count);
    printf("Baseline: %s\n", profile->baseline.baseline_established ? "Established" : "Learning...");
    
    if (profile->baseline.baseline_established) {
        printf("Read IOPS:   mean=%.2f, std=%.2f\n", profile->baseline.mean_read_iops, profile->baseline.std_read_iops);
        printf("Write IOPS:  mean=%.2f, std=%.2f\n", profile->baseline.mean_write_iops, profile->baseline.std_write_iops);
        printf("Avg Entropy: mean=%.4f, std=%.4f\n", profile->baseline.mean_avg_entropy, profile->baseline.std_avg_entropy);
        printf("Max Entropy: mean=%.4f, std=%.4f\n", profile->baseline.mean_max_entropy, profile->baseline.std_max_entropy);
        printf("Write Ratio: mean=%.4f, std=%.4f\n", profile->baseline.mean_write_ratio, profile->baseline.std_write_ratio);
    }
    printf("================================\n\n");
    
    pthread_mutex_unlock(&learner->lock);
}

void behavior_learner_destroy(BehaviorLearner *learner) {
    pthread_mutex_lock(&learner->lock);
    
    for (int i = 0; i < learner->profile_count; i++) {
        pthread_mutex_destroy(&learner->profiles[i].lock);
    }
    
    if (learner->baseline_file) {
        fclose(learner->baseline_file);
        learner->baseline_file = NULL;
    }
    
    learner->profile_count = 0;
    pthread_mutex_unlock(&learner->lock);
    pthread_mutex_destroy(&learner->lock);
}
