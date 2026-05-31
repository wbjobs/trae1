#ifndef RANSOMWARE_DETECTOR_H
#define RANSOMWARE_DETECTOR_H

#include "common.h"
#include "entropy.h"

#define MAX_SUSPICIOUS_EXTENSIONS 16
#define MAX_DECOY_FILES 100
#define MAX_LOG_RETENTION 10
#define BEHAVIOR_LEARN_DAYS 7
#define BEHAVIOR_SAMPLE_INTERVAL 3600
#define MAX_BEHAVIOR_SAMPLES 168

typedef struct {
    char extension[16];
    bool active;
} SuspiciousExtension;

typedef struct {
    char file_path[MAX_PATH_LENGTH];
    time_t creation_time;
    bool is_modified;
    bool is_accessed;
} DecoyFile;

typedef struct {
    char username[MAX_USERNAME];
    char client_ip[64];
    time_t timestamp;
    char file_path[MAX_PATH_LENGTH];
    char action[16];
    double entropy;
    size_t bytes_written;
} AccessLog;

typedef struct {
    double avg_read_iops;
    double avg_write_iops;
    double avg_entropy;
    double max_entropy;
    size_t avg_file_size;
    size_t files_created_per_hour;
    size_t files_modified_per_hour;
    size_t files_deleted_per_hour;
    double avg_write_ratio;
} BehaviorBaseline;

typedef struct {
    char username[MAX_USERNAME];
    BehaviorBaseline baseline;
    BehaviorBaseline samples[MAX_BEHAVIOR_SAMPLES];
    int sample_count;
    time_t last_update;
    bool baseline_established;
} UserBehaviorProfile;

typedef struct {
    bool enabled;
    bool quarantine_mode;
    bool read_only_mode;
    double entropy_threshold;
    SuspiciousExtension extensions[MAX_SUSPICIOUS_EXTENSIONS];
    int extension_count;
    DecoyFile decoy_files[MAX_DECOY_FILES];
    int decoy_count;
    AccessLog access_logs[MAX_LOG_RETENTION * 600];
    int log_count;
    int log_start;
    UserBehaviorProfile profiles[MAX_SESSIONS];
    int profile_count;
    pthread_mutex_t lock;
    FILE *alert_log;
    char quarantine_dir[MAX_PATH_LENGTH];
    time_t startup_time;
} RansomwareDetector;

int ransomware_detector_init(RansomwareDetector *detector, const char *log_dir);
int ransomware_detector_add_suspicious_extension(RansomwareDetector *detector, const char *extension);
int ransomware_detector_add_decoy_file(RansomwareDetector *detector, const char *file_path);
int ransomware_detector_deploy_decoys(RansomwareDetector *detector, const char *base_path);
int ransomware_detector_monitor_write(RansomwareDetector *detector, const char *username, 
                                       const char *client_ip, const char *file_path,
                                       const unsigned char *data, size_t length);
int ransomware_detector_monitor_rename(RansomwareDetector *detector, const char *username,
                                        const char *client_ip, const char *old_path,
                                        const char *new_path);
int ransomware_detector_monitor_delete(RansomwareDetector *detector, const char *username,
                                        const char *client_ip, const char *file_path);
int ransomware_detector_check_decoy_access(RansomwareDetector *detector, const char *file_path);
int ransomware_detector_quarantine_client(RansomwareDetector *detector, const char *client_ip);
int ransomware_detector_freeze_share(RansomwareDetector *detector);
int ransomware_detector_update_behavior(RansomwareDetector *detector, const char *username,
                                         double read_iops, double write_iops,
                                         double entropy, size_t bytes_written);
bool ransomware_detector_is_behavior_anomalous(RansomwareDetector *detector, const char *username,
                                                double read_iops, double write_iops,
                                                double entropy);
int ransomware_detector_log_access(RansomwareDetector *detector, const char *username,
                                    const char *client_ip, const char *file_path,
                                    const char *action, double entropy, size_t bytes);
int ransomware_detector_get_recent_logs(RansomwareDetector *detector, AccessLog *logs, int max_count);
void ransomware_detector_stats(RansomwareDetector *detector);
void ransomware_detector_destroy(RansomwareDetector *detector);

#endif
