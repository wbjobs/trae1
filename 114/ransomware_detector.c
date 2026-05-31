#include "ransomware_detector.h"
#include <sys/stat.h>
#include <time.h>

static const char *default_suspicious_extensions[] = {
    ".enc", ".locked", ".crypt", ".encrypted", ".ransom",
    ".locked", ".crypto", ".aes", ".rsa", ".bitcoin",
    NULL
};

static int is_suspicious_extension(RansomwareDetector *detector, const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    
    for (int i = 0; i < detector->extension_count; i++) {
        if (strcasecmp(ext, detector->extensions[i].extension) == 0) {
            return 1;
        }
    }
    return 0;
}

static void write_alert(RansomwareDetector *detector, const char *format, ...) {
    if (!detector->alert_log) return;
    
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(detector->alert_log, "[%s] ALERT: ", time_str);
    
    va_list args;
    va_start(args, format);
    vfprintf(detector->alert_log, format, args);
    va_end(args);
    
    fprintf(detector->alert_log, "\n");
    fflush(detector->alert_log);
    
    printf("[%s] SECURITY ALERT: ", time_str);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

int ransomware_detector_init(RansomwareDetector *detector, const char *log_dir) {
    memset(detector, 0, sizeof(RansomwareDetector));
    detector->enabled = true;
    detector->quarantine_mode = false;
    detector->read_only_mode = false;
    detector->entropy_threshold = ENTROPY_THRESHOLD;
    detector->startup_time = time(NULL);

    if (pthread_mutex_init(&detector->lock, NULL) != 0) {
        return -1;
    }

    char alert_path[MAX_PATH_LENGTH];
    snprintf(alert_path, sizeof(alert_path), "%s/ransomware_alerts.log", 
             log_dir && strlen(log_dir) > 0 ? log_dir : ".");
    
    detector->alert_log = fopen(alert_path, "a");
    if (!detector->alert_log) {
        detector->alert_log = fopen("ransomware_alerts.log", "a");
    }

    snprintf(detector->quarantine_dir, sizeof(detector->quarantine_dir), 
             "%s/quarantine", log_dir && strlen(log_dir) > 0 ? log_dir : ".");
    
    mkdir(detector->quarantine_dir, 0700);

    for (int i = 0; default_suspicious_extensions[i] != NULL; i++) {
        if (detector->extension_count < MAX_SUSPICIOUS_EXTENSIONS) {
            strncpy(detector->extensions[detector->extension_count].extension, 
                    default_suspicious_extensions[i], 15);
            detector->extensions[detector->extension_count].active = true;
            detector->extension_count++;
        }
    }

    return 0;
}

int ransomware_detector_add_suspicious_extension(RansomwareDetector *detector, const char *extension) {
    pthread_mutex_lock(&detector->lock);
    
    if (detector->extension_count >= MAX_SUSPICIOUS_EXTENSIONS) {
        pthread_mutex_unlock(&detector->lock);
        return -1;
    }
    
    char ext[16];
    if (extension[0] != '.') {
        snprintf(ext, sizeof(ext), ".%s", extension);
    } else {
        strncpy(ext, extension, 15);
    }
    
    strncpy(detector->extensions[detector->extension_count].extension, ext, 15);
    detector->extensions[detector->extension_count].active = true;
    detector->extension_count++;
    
    pthread_mutex_unlock(&detector->lock);
    return 0;
}

int ransomware_detector_add_decoy_file(RansomwareDetector *detector, const char *file_path) {
    pthread_mutex_lock(&detector->lock);
    
    if (detector->decoy_count >= MAX_DECOY_FILES) {
        pthread_mutex_unlock(&detector->lock);
        return -1;
    }
    
    strncpy(detector->decoy_files[detector->decoy_count].file_path, file_path, MAX_PATH_LENGTH - 1);
    detector->decoy_files[detector->decoy_count].creation_time = time(NULL);
    detector->decoy_files[detector->decoy_count].is_modified = false;
    detector->decoy_files[detector->decoy_count].is_accessed = false;
    detector->decoy_count++;
    
    pthread_mutex_unlock(&detector->lock);
    return 0;
}

int ransomware_detector_deploy_decoys(RansomwareDetector *detector, const char *base_path) {
    if (!base_path) return -1;
    
    const char *decoy_names[] = {
        "important_documents.docx", "financial_report.xlsx", 
        "employee_data.csv", "passwords.doc",
        "secret_notes.txt", "confidential.pdf",
        NULL
    };
    
    char content[] = "This is a decoy file used for ransomware detection.\n"
                     "Do not modify or delete this file.\n"
                     "Any unauthorized access will trigger security alerts.\n";
    
    for (int i = 0; decoy_names[i] != NULL; i++) {
        char path[MAX_PATH_LENGTH];
        snprintf(path, sizeof(path), "%s/%s", base_path, decoy_names[i]);
        
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%s", content);
            fclose(f);
            
            struct stat st;
            if (stat(path, &st) == 0) {
                ransomware_detector_add_decoy_file(detector, path);
            }
        }
    }
    
    printf("Deployed %d decoy files in %s\n", detector->decoy_count, base_path);
    return 0;
}

int ransomware_detector_monitor_write(RansomwareDetector *detector, const char *username,
                                       const char *client_ip, const char *file_path,
                                       const unsigned char *data, size_t length) {
    pthread_mutex_lock(&detector->lock);
    
    double entropy = calculate_shannon_entropy(data, length > 4096 ? 4096 : length);
    
    ransomware_detector_log_access(detector, username, client_ip, file_path, 
                                    "WRITE", entropy, length);
    
    if (entropy > detector->entropy_threshold && is_suspicious_extension(detector, file_path)) {
        write_alert(detector, "High entropy write detected: %.2f for file: %s by %s@%s", 
                   entropy, file_path, username, client_ip);
        
        detector->quarantine_mode = true;
        detector->read_only_mode = true;
        
        pthread_mutex_unlock(&detector->lock);
        return -1;
    }
    
    if (entropy > detector->entropy_threshold) {
        write_alert(detector, "High entropy write detected: %.2f for file: %s by %s@%s (monitoring)", 
                   entropy, file_path, username, client_ip);
    }
    
    pthread_mutex_unlock(&detector->lock);
    return 0;
}

int ransomware_detector_monitor_rename(RansomwareDetector *detector, const char *username,
                                        const char *client_ip, const char *old_path,
                                        const char *new_path) {
    pthread_mutex_lock(&detector->lock);
    
    ransomware_detector_log_access(detector, username, client_ip, old_path, 
                                    "RENAME_FROM", 0, 0);
    ransomware_detector_log_access(detector, username, client_ip, new_path, 
                                    "RENAME_TO", 0, 0);
    
    if (is_suspicious_extension(detector, new_path)) {
        write_alert(detector, "Suspicious rename detected: %s -> %s by %s@%s", 
                   old_path, new_path, username, client_ip);
        
        detector->quarantine_mode = true;
        detector->read_only_mode = true;
        
        pthread_mutex_unlock(&detector->lock);
        return -1;
    }
    
    pthread_mutex_unlock(&detector->lock);
    return 0;
}

int ransomware_detector_monitor_delete(RansomwareDetector *detector, const char *username,
                                        const char *client_ip, const char *file_path) {
    pthread_mutex_lock(&detector->lock);
    
    ransomware_detector_log_access(detector, username, client_ip, file_path, 
                                    "DELETE", 0, 0);
    
    if (ransomware_detector_check_decoy_access(detector, file_path)) {
        write_alert(detector, "Decoy file deletion attempt: %s by %s@%s", 
                   file_path, username, client_ip);
    }
    
    pthread_mutex_unlock(&detector->lock);
    return 0;
}

int ransomware_detector_check_decoy_access(RansomwareDetector *detector, const char *file_path) {
    for (int i = 0; i < detector->decoy_count; i++) {
        if (strcmp(detector->decoy_files[i].file_path, file_path) == 0) {
            detector->decoy_files[i].is_accessed = true;
            return 1;
        }
    }
    return 0;
}

int ransomware_detector_quarantine_client(RansomwareDetector *detector, const char *client_ip) {
    pthread_mutex_lock(&detector->lock);
    
    write_alert(detector, "Client quarantined: %s", client_ip);
    
    char ip_dir[MAX_PATH_LENGTH];
    snprintf(ip_dir, sizeof(ip_dir), "%s/%s", detector->quarantine_dir, client_ip);
    mkdir(ip_dir, 0700);
    
    detector->quarantine_mode = true;
    
    pthread_mutex_unlock(&detector->lock);
    return 0;
}

int ransomware_detector_freeze_share(RansomwareDetector *detector) {
    pthread_mutex_lock(&detector->lock);
    
    write_alert(detector, "Share frozen - entering read-only mode");
    
    detector->read_only_mode = true;
    
    pthread_mutex_unlock(&detector->lock);
    return 0;
}

int ransomware_detector_update_behavior(RansomwareDetector *detector, const char *username,
                                         double read_iops, double write_iops,
                                         double entropy, size_t bytes_written) {
    (void)detector;
    (void)username;
    (void)read_iops;
    (void)write_iops;
    (void)entropy;
    (void)bytes_written;
    return 0;
}

bool ransomware_detector_is_behavior_anomalous(RansomwareDetector *detector, const char *username,
                                                double read_iops, double write_iops,
                                                double entropy) {
    (void)detector;
    (void)username;
    (void)read_iops;
    (void)write_iops;
    
    return entropy > detector->entropy_threshold;
}

int ransomware_detector_log_access(RansomwareDetector *detector, const char *username,
                                    const char *client_ip, const char *file_path,
                                    const char *action, double entropy, size_t bytes) {
    int idx = (detector->log_start + detector->log_count) % (MAX_LOG_RETENTION * 600);
    
    strncpy(detector->access_logs[idx].username, username, MAX_USERNAME - 1);
    strncpy(detector->access_logs[idx].client_ip, client_ip, 63);
    strncpy(detector->access_logs[idx].file_path, file_path, MAX_PATH_LENGTH - 1);
    strncpy(detector->access_logs[idx].action, action, 15);
    detector->access_logs[idx].timestamp = time(NULL);
    detector->access_logs[idx].entropy = entropy;
    detector->access_logs[idx].bytes_written = bytes;
    
    if (detector->log_count >= MAX_LOG_RETENTION * 600) {
        detector->log_start = (detector->log_start + 1) % (MAX_LOG_RETENTION * 600);
    } else {
        detector->log_count++;
    }
    
    return 0;
}

int ransomware_detector_get_recent_logs(RansomwareDetector *detector, AccessLog *logs, int max_count) {
    pthread_mutex_lock(&detector->lock);
    
    int count = detector->log_count < max_count ? detector->log_count : max_count;
    
    for (int i = 0; i < count; i++) {
        int idx = (detector->log_start + detector->log_count - count + i) % (MAX_LOG_RETENTION * 600);
        memcpy(&logs[i], &detector->access_logs[idx], sizeof(AccessLog));
    }
    
    pthread_mutex_unlock(&detector->lock);
    return count;
}

void ransomware_detector_stats(RansomwareDetector *detector) {
    pthread_mutex_lock(&detector->lock);
    
    printf("\n=== Ransomware Detector Stats ===\n");
    printf("Enabled: %s\n", detector->enabled ? "Yes" : "No");
    printf("Quarantine Mode: %s\n", detector->quarantine_mode ? "ACTIVE" : "Normal");
    printf("Read-Only Mode: %s\n", detector->read_only_mode ? "ACTIVE" : "Normal");
    printf("Entropy Threshold: %.2f\n", detector->entropy_threshold);
    printf("Suspicious Extensions: %d\n", detector->extension_count);
    for (int i = 0; i < detector->extension_count; i++) {
        if (detector->extensions[i].active) {
            printf("  - %s\n", detector->extensions[i].extension);
        }
    }
    printf("Decoy Files: %d\n", detector->decoy_count);
    printf("Access Logs: %d\n", detector->log_count);
    printf("Behavior Profiles: %d\n", detector->profile_count);
    printf("=================================\n\n");
    
    pthread_mutex_unlock(&detector->lock);
}

void ransomware_detector_destroy(RansomwareDetector *detector) {
    pthread_mutex_lock(&detector->lock);
    
    if (detector->alert_log) {
        fclose(detector->alert_log);
        detector->alert_log = NULL;
    }
    
    pthread_mutex_unlock(&detector->lock);
    pthread_mutex_destroy(&detector->lock);
}
