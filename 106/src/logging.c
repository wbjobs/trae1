#include "nvme_hotplug_cli.h"

static FILE *audit_fp = NULL;
static pthread_mutex_t audit_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_to_syslog(int priority, const char *format, ...) {
    va_list args;
    char message[1024];

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    syslog(priority, "%s", message);
}

void log_audit(const char *operation, const char *device, const char *details) {
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    pthread_mutex_lock(&audit_mutex);

    if (audit_fp == NULL) {
        mkdir(AUDIT_LOG_DIR, 0755);
        audit_fp = fopen(AUDIT_LOG_FILE, "a");
        if (!audit_fp) {
            pthread_mutex_unlock(&audit_mutex);
            log_to_syslog(LOG_ERR, "Failed to open audit log: %s", strerror(errno));
            return;
        }
    }

    fprintf(audit_fp, "[%s] operation=%s device=%s details=%s\n",
            timestamp, operation, device ? device : "N/A", details ? details : "N/A");
    fflush(audit_fp);

    pthread_mutex_unlock(&audit_mutex);

    log_to_syslog(LOG_INFO, "AUDIT: operation=%s device=%s details=%s",
                   operation, device ? device : "N/A", details ? details : "N/A");
}

void log_event(int priority, const char *event_type, const char *device,
               const char *message, ...) {
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    char full_message[2048];
    va_list args;
    va_start(args, message);
    vsnprintf(full_message, sizeof(full_message), message, args);
    va_end(args);

    pthread_mutex_lock(&audit_mutex);

    if (audit_fp == NULL) {
        mkdir(AUDIT_LOG_DIR, 0755);
        audit_fp = fopen(AUDIT_LOG_FILE, "a");
    }

    if (audit_fp) {
        fprintf(audit_fp, "[%s] event=%s device=%s %s\n",
                timestamp, event_type, device ? device : "N/A", full_message);
        fflush(audit_fp);
    }

    pthread_mutex_unlock(&audit_mutex);

    log_to_syslog(priority, "[%s] device=%s %s", event_type, device ? device : "N/A", full_message);
}

int open_audit_log(void) {
    pthread_mutex_lock(&audit_mutex);

    if (audit_fp != NULL) {
        pthread_mutex_unlock(&audit_mutex);
        return 0;
    }

    mkdir(AUDIT_LOG_DIR, 0755);
    audit_fp = fopen(AUDIT_LOG_FILE, "a");
    if (!audit_fp) {
        pthread_mutex_unlock(&audit_mutex);
        return -1;
    }

    pthread_mutex_unlock(&audit_mutex);
    return 0;
}

void close_audit_log(void) {
    pthread_mutex_lock(&audit_mutex);

    if (audit_fp != NULL) {
        fclose(audit_fp);
        audit_fp = NULL;
    }

    pthread_mutex_unlock(&audit_mutex);
}

int read_audit_log(const char *log_file, int max_entries, void (*callback)(const char *entry)) {
    FILE *f = fopen(log_file ? log_file : AUDIT_LOG_FILE, "r");
    if (!f) {
        return -1;
    }

    char line[2048];
    int count = 0;

    while (fgets(line, sizeof(line), f) != NULL && count < max_entries) {
        if (callback) {
            callback(line);
        }
        count++;
    }

    fclose(f);
    return count;
}

void log_device_state_change(const char *device, device_state_t old_state,
                             device_state_t new_state) {
    char old_str[64], new_str[64];
    snprintf(old_str, sizeof(old_str), "%s", device_state_str(old_state));
    snprintf(new_str, sizeof(new_str), "%s", device_state_str(new_state));

    log_audit("state_change", device, old_str);
    log_event(LOG_INFO, "STATE_CHANGE", device, "State changed from %s to %s", old_str, new_str);
}

void log_error(const char *context, const char *device, const char *error_msg) {
    log_event(LOG_ERR, "ERROR", device, "%s: %s", context, error_msg);
}

void log_warning(const char *context, const char *device, const char *warning_msg) {
    log_event(LOG_WARNING, "WARNING", device, "%s: %s", context, warning_msg);
}

void log_info(const char *context, const char *device, const char *info_msg) {
    log_event(LOG_INFO, "INFO", device, "%s: %s", context, info_msg);
}
