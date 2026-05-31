#ifndef NVME_HOTPLUG_CLI_H
#define NVME_HOTPLUG_CLI_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <syslog.h>
#include <libgen.h>
#include <limits.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>

#define MAX_NVME_DEVICES 32
#define MAX_MOUNT_POINTS 32
#define MAX_PENDING_IO 256
#define MAX_PATH_LEN 256
#define MAX_NAME_LEN 128
#define UEVENT_BUF_SIZE 2048
#define SYSFS_NVME_PATH "/sys/class/nvme"
#define PROC_MOUNTS "/proc/mounts"
#define DEFAULT_MOUNT_BASE "/mnt/nvme"
#define AUDIT_LOG_DIR "/var/log/nvme-hotplug"
#define AUDIT_LOG_FILE AUDIT_LOG_DIR "/audit.log"
#define PCI_CONFIG_SPACE_SIZE 256
#define DEVICE_CHECK_INTERVAL_MS 500
#define RECOVERY_TIMEOUT_SEC 30
#define PREDICTION_MODEL_DIR "/var/lib/nvme-hotplug"
#define PREDICTION_MODEL_FILE PREDICTION_MODEL_DIR "/prediction_model.dat"
#define PREDICTION_TRAINING_DATA PREDICTION_MODEL_DIR "/training_data.csv"
#define FAILURE_PROBABILITY_THRESHOLD 80.0
#define MIGRATION_PROGRESS_INTERVAL 5

#define PREDICTION_DAYS_AHEAD 30
#define MIN_TRAINING_SAMPLES 100
#define MODEL_RETRAIN_INTERVAL_DAYS 30

typedef enum {
    FS_TYPE_EXT4,
    FS_TYPE_XFS,
    FS_TYPE_NONE
} filesystem_type_t;

typedef enum {
    RAID_NONE,
    RAID0,
    RAID1
} raid_level_t;

typedef enum {
    DEVICE_STATE_UNKNOWN,
    DEVICE_STATE_DETECTED,
    DEVICE_STATE_BINDING,
    DEVICE_STATE_INITIALIZED,
    DEVICE_STATE_FORMATTED,
    DEVICE_STATE_MOUNTED,
    DEVICE_STATE_RAID_MEMBER,
    DEVICE_STATE_REMOVING,
    DEVICE_STATE_REMOVED,
    DEVICE_STATE_REMOVAL_DETECTED,
    DEVICE_STATE_RECOVERY_IN_PROGRESS,
    DEVICE_STATE_RECOVERY_FAILED,
    DEVICE_STATE_READONLY,
    DEVICE_STATE_MIGRATING,
    DEVICE_STATE_MIGRATED
} device_state_t;

typedef enum {
    IO_STATE_PENDING,
    IO_STATE_COMPLETED,
    IO_STATE_FAILED,
    IO_STATE_CANCELLED
} io_state_t;

typedef enum {
    HEALTH_STATUS_HEALTHY,
    HEALTH_STATUS_WARNING,
    HEALTH_STATUS_CRITICAL,
    HEALTH_STATUS_FAILURE
} health_status_t;

typedef struct pending_io pending_io_t;

typedef void (*io_callback_t)(void *arg, int status);

struct pending_io {
    uint64_t io_id;
    void *buffer;
    size_t size;
    off_t offset;
    int type;
    io_state_t state;
    io_callback_t callback;
    void *callback_arg;
    time_t submit_time;
    struct pending_io *next;
};

typedef struct {
    pending_io_t *head;
    pending_io_t *tail;
    int count;
    uint64_t next_io_id;
    pthread_mutex_t mutex;
} io_queue_t;

typedef struct {
    bool removal_detected;
    bool pci_space_accessible;
    bool uevent_received;
    time_t removal_time;
    int pci_vendor;
    int pci_device;
    uint32_t pci_cmd;
    uint8_t pci_header_type;
    bool device_expected;
    pthread_mutex_t detection_mutex;
} removal_detection_t;

typedef struct {
    bool recovery_in_progress;
    bool recovery_success;
    time_t recovery_start_time;
    int recovery_attempts;
    bool force_rebuild;
    bool data_integrity_checked;
    bool data_corrupted;
} recovery_context_t;

typedef struct {
    uint64_t reallocated_sectors;
    uint64_t power_on_hours;
    int temperature;
    uint8_t available_spare;
    uint8_t percentage_used;
    uint64_t total_data_written;
    uint64_t total_data_read;
    uint8_t unsafe_shutdowns;
    uint64_t media_errors;
    uint64_t power_cycles;
    uint8_t spare_exhausted;
    float temperature_normalized;
    float hours_normalized;
    float spare_normalized;
    float reallocated_normalized;
} smart_features_t;

typedef struct {
    double failure_probability;
    double confidence_interval_low;
    double confidence_interval_high;
    health_status_t status;
    time_t prediction_time;
    int days_until_failure_estimate;
    char risk_factors[MAX_PATH_LEN];
    char recommendations[MAX_PATH_LEN];
} prediction_result_t;

typedef struct {
    char source_pci[MAX_NAME_LEN];
    char target_pci[MAX_NAME_LEN];
    char source_mount[MAX_PATH_LEN];
    char target_mount[MAX_PATH_LEN];
    float progress;
    time_t start_time;
    time_t estimated_completion;
    bool using_raid1_mirror;
    char raid_name[MAX_NAME_LEN];
    int status;
} migration_context_t;

typedef struct {
    int tree_count;
    int max_depth;
    int min_samples_leaf;
    float feature_importances[8];
    float thresholds[8][2];
    int feature_indices[8];
    bool trained;
    time_t last_training_time;
} random_forest_model_t;

typedef struct {
    char name[MAX_NAME_LEN];
    char pci_addr[MAX_NAME_LEN];
    char serial[MAX_NAME_LEN];
    char model[MAX_NAME_LEN];
    char firmware_rev[MAX_NAME_LEN];
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t nsid;
    uint64_t capacity;
    uint32_t block_size;
    device_state_t state;
    filesystem_type_t fs_type;
    char mount_point[MAX_PATH_LEN];
    bool is_raid_member;
    raid_level_t raid_level;
    char raid_name[MAX_NAME_LEN];
    int temperature;
    uint8_t percent_used;
    uint64_t data_units_read;
    uint64_t data_units_written;
    uint64_t power_cycles;
    uint64_t power_on_hours;
    uint8_t media_errors;
    uint32_t pci_link_speed;
    uint32_t pci_link_width;
    time_t last_seen;
    io_queue_t pending_ios;
    removal_detection_t removal_ctx;
    recovery_context_t recovery_ctx;
    bool readonly_mode;
    pthread_mutex_t device_mutex;
    int ctrlr_fd;
    bool ctrlr_connected;
    smart_features_t smart_data;
    prediction_result_t prediction;
    migration_context_t migration;
} nvme_device_t;

typedef struct {
    nvme_device_t devices[MAX_NVME_DEVICES];
    int device_count;
    pthread_mutex_t mutex;
    bool monitoring;
    int uevent_sock;
    pthread_t detection_thread;
    bool detection_thread_running;
    random_forest_model_t prediction_model;
    pthread_mutex_t model_mutex;
} nvme_manager_t;

typedef struct {
    char action[MAX_NAME_LEN];
    char devname[MAX_NAME_LEN];
    char subsystem[MAX_NAME_LEN];
    char pci_addr[MAX_NAME_LEN];
    time_t timestamp;
} uevent_t;

typedef struct {
    char name[MAX_NAME_LEN];
    char device[MAX_NAME_LEN];
    char mount_point[MAX_PATH_LEN];
    filesystem_type_t fs_type;
    bool auto_mount;
} mount_config_t;

typedef struct {
    raid_level_t level;
    char name[MAX_NAME_LEN];
    char member_devices[MAX_NVME_DEVICES][MAX_NAME_LEN];
    int member_count;
    uint64_t stripe_size;
} raid_config_t;

typedef struct {
    bool daemon_mode;
    bool verbose;
    bool force_recover;
    int log_level;
    char config_file[MAX_PATH_LEN];
    char mount_base[MAX_PATH_LEN];
    filesystem_type_t default_fs;
    raid_level_t default_raid;
    mount_config_t mounts[MAX_MOUNT_POINTS];
    int mount_count;
} cli_config_t;

int nvme_manager_init(nvme_manager_t *mgr);
void nvme_manager_destroy(nvme_manager_t *mgr);
int discover_nvme_devices(nvme_manager_t *mgr);
int handle_device_add(nvme_manager_t *mgr, const char *pci_addr);
int handle_device_remove(nvme_manager_t *mgr, const char *pci_addr);
int bind_nvme_driver(const char *pci_addr, const char *driver);
int unbind_nvme_driver(const char *pci_addr);
int init_spdk_nvme_controller(nvme_device_t *dev);
int cleanup_spdk_nvme_controller(nvme_device_t *dev);
int format_device(nvme_device_t *dev, filesystem_type_t fs_type);
int mount_device(nvme_device_t *dev, const char *mount_point);
int unmount_device(nvme_device_t *dev);
int create_raid_volume(raid_config_t *config);
int destroy_raid_volume(raid_config_t *config);
int get_device_health(nvme_device_t *dev);
int export_smart_log(nvme_device_t *dev, const char *output_file);
int start_monitoring(nvme_manager_t *mgr);
void stop_monitoring(nvme_manager_t *mgr);
int process_uevent(nvme_manager_t *mgr, const char *uevent_msg);
int list_devices(nvme_manager_t *mgr);
void log_audit(const char *operation, const char *device, const char *details);
void log_to_syslog(int priority, const char *format, ...);

int cli_parse_args(int argc, char *argv[], cli_config_t *config);
int cli_list(nvme_manager_t *mgr);
int cli_monitor(nvme_manager_t *mgr);
int cli_add_device(nvme_manager_t *mgr, const char *pci_addr);
int cli_remove_device(nvme_manager_t *mgr, const char *pci_addr);
int cli_format(nvme_manager_t *mgr, const char *pci_addr, filesystem_type_t fs_type);
int cli_mount(nvme_manager_t *mgr, const char *pci_addr, const char *mount_point);
int cli_unmount(nvme_manager_t *mgr, const char *pci_addr);
int cli_raid_create(raid_config_t *config);
int cli_raid_destroy(raid_config_t *config);
int cli_smart_export(nvme_manager_t *mgr, const char *pci_addr, const char *output_file);
int cli_force_recover(nvme_manager_t *mgr, const char *pci_addr);
int cli_predict(nvme_manager_t *mgr, const char *pci_addr);
int cli_migrate(nvme_manager_t *mgr, const char *source_pci, const char *target_pci);

int init_pending_io_queue(io_queue_t *queue);
void destroy_pending_io_queue(io_queue_t *queue);
pending_io_t* submit_pending_io(io_queue_t *queue, void *buffer, size_t size,
                                off_t offset, int type, io_callback_t callback, void *arg);
int complete_pending_io(io_queue_t *queue, uint64_t io_id, int status);
int cancel_all_pending_io(io_queue_t *queue, int status);
int abort_stale_pending_io(io_queue_t *queue, int timeout_sec);

int start_removal_detection(nvme_manager_t *mgr);
void stop_removal_detection(nvme_manager_t *mgr);
int check_device_presence(nvme_device_t *dev);
int read_pci_config_space(const char *pci_addr, void *buffer, size_t size, off_t offset);
bool is_device_present(const char *pci_addr);
int handle_device_removal_detected(nvme_manager_t *mgr, nvme_device_t *dev);
int handle_device_reinserted(nvme_manager_t *mgr, nvme_device_t *dev);

int switch_filesystem_readonly(nvme_device_t *dev);
int remount_readonly(const char *mount_point);
int check_filesystem_corruption(nvme_device_t *dev);
int attempt_device_recovery(nvme_device_t *dev, bool force_rebuild);
int rebuild_bdev(nvme_device_t *dev);

int collect_smart_features(nvme_device_t *dev, smart_features_t *features);
int predict_disk_failure(nvme_manager_t *mgr, nvme_device_t *dev, prediction_result_t *result);
int init_prediction_model(nvme_manager_t *mgr);
int train_random_forest(nvme_manager_t *mgr);
int save_prediction_model(nvme_manager_t *mgr);
int load_prediction_model(nvme_manager_t *mgr);
float calculate_feature_importance(int feature_idx, float *feature_values, int num_samples);
double calculate_failure_probability(random_forest_model_t *model, smart_features_t *features);
int check_and_alert_predictions(nvme_manager_t *mgr);
int add_training_sample(smart_features_t *features, bool failed);
int generate_synthetic_training_data(void);
int retrain_model_if_needed(nvme_manager_t *mgr);

int migrate_data_online(nvme_device_t *source, nvme_device_t *target);
int migrate_with_raid1_mirror(nvme_device_t *source, nvme_device_t *target, const char *raid_name);
int start_migration(nvme_manager_t *mgr, nvme_device_t *source, nvme_device_t *target);
int complete_migration(nvme_manager_t *mgr, nvme_device_t *source, nvme_device_t *target);
int rollback_migration(nvme_device_t *source, nvme_device_t *target);
int update_fstab_entry(const char *old_device, const char *new_device, const char *mount_point);
int verify_migration_integrity(nvme_device_t *source, nvme_device_t *target);
const char* health_status_str(health_status_t status);

void print_device_info(nvme_device_t *dev);
void print_device_health(nvme_device_t *dev);
void print_prediction_result(nvme_device_t *dev);
const char* filesystem_type_str(filesystem_type_t type);
const char* raid_level_str(raid_level_t level);
const char* device_state_str(device_state_t state);

#endif
