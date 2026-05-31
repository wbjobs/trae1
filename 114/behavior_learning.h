#ifndef BEHAVIOR_LEARNING_H
#define BEHAVIOR_LEARNING_H

#include "common.h"

#define BEHAVIOR_LEARN_PERIOD (7 * 24 * 3600)
#define BEHAVIOR_SAMPLE_INTERVAL 3600
#define BEHAVIOR_MAX_SAMPLES 504
#define BEHAVIOR_ANOMALY_FACTOR 3.0

typedef struct {
    time_t timestamp;
    double read_iops;
    double write_iops;
    double avg_entropy;
    double max_entropy;
    size_t bytes_written;
    size_t files_created;
    size_t files_modified;
    size_t files_deleted;
    size_t files_renamed;
    double write_ratio;
} BehaviorSample;

typedef struct {
    double mean_read_iops;
    double std_read_iops;
    double mean_write_iops;
    double std_write_iops;
    double mean_avg_entropy;
    double std_avg_entropy;
    double mean_max_entropy;
    double std_max_entropy;
    double mean_write_ratio;
    double std_write_ratio;
    size_t mean_files_created_per_hour;
    size_t mean_files_modified_per_hour;
    size_t mean_files_deleted_per_hour;
    size_t mean_files_renamed_per_hour;
    bool baseline_established;
    int sample_count;
    time_t last_update;
} BehaviorBaseline;

typedef struct {
    char username[MAX_USERNAME];
    BehaviorSample samples[BEHAVIOR_MAX_SAMPLES];
    int sample_count;
    BehaviorBaseline baseline;
    BehaviorSample current_hour;
    time_t current_hour_start;
    pthread_mutex_t lock;
} UserBehaviorProfile;

typedef struct {
    UserBehaviorProfile profiles[MAX_SESSIONS];
    int profile_count;
    pthread_mutex_t lock;
    time_t startup_time;
    FILE *baseline_file;
} BehaviorLearner;

int behavior_learner_init(BehaviorLearner *learner, const char *data_dir);
int behavior_learner_add_sample(BehaviorLearner *learner, const char *username, 
                                 const BehaviorSample *sample);
int behavior_learner_update_current(BehaviorLearner *learner, const char *username,
                                     double read_iops, double write_iops,
                                     double avg_entropy, double max_entropy,
                                     size_t bytes_written, size_t files_created,
                                     size_t files_modified, size_t files_deleted,
                                     size_t files_renamed);
bool behavior_learner_is_anomalous(BehaviorLearner *learner, const char *username,
                                    double read_iops, double write_iops,
                                    double avg_entropy, double max_entropy,
                                    double write_ratio);
double behavior_learner_get_anomaly_score(BehaviorLearner *learner, const char *username,
                                           double read_iops, double write_iops,
                                           double avg_entropy, double max_entropy,
                                           double write_ratio);
int behavior_learner_establish_baseline(BehaviorLearner *learner, const char *username);
int behavior_learner_save_baselines(BehaviorLearner *learner, const char *path);
int behavior_learner_load_baselines(BehaviorLearner *learner, const char *path);
void behavior_learner_stats(BehaviorLearner *learner, const char *username);
void behavior_learner_destroy(BehaviorLearner *learner);

#endif
