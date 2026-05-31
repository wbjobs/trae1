#include "nvme_hotplug_cli.h"

static void* removal_detection_thread(void *arg);

int read_pci_config_space(const char *pci_addr, void *buffer, size_t size, off_t offset) {
    if (!pci_addr || !buffer) return -1;

    char config_path[MAX_PATH_LEN];
    snprintf(config_path, sizeof(config_path), "/sys/bus/pci/devices/%s/config", pci_addr);

    int fd = open(config_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }

    ssize_t bytes_read = read(fd, buffer, size);
    close(fd);

    if (bytes_read != (ssize_t)size) {
        return -1;
    }

    return 0;
}

bool is_device_present(const char *pci_addr) {
    if (!pci_addr) return false;

    char device_path[MAX_PATH_LEN];
    snprintf(device_path, sizeof(device_path), "/sys/bus/pci/devices/%s", pci_addr);

    struct stat st;
    if (stat(device_path, &st) != 0) {
        return false;
    }

    uint16_t vendor_id = 0xFFFF;
    if (read_pci_config_space(pci_addr, &vendor_id, sizeof(vendor_id), 0) != 0) {
        return false;
    }

    if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
        return false;
    }

    return true;
}

int check_device_presence(nvme_device_t *dev) {
    if (!dev) return -1;

    pthread_mutex_lock(&dev->removal_ctx.detection_mutex);

    bool was_present = !dev->removal_ctx.removal_detected;
    bool now_present = is_device_present(dev->pci_addr);

    if (was_present && !now_present) {
        dev->removal_ctx.removal_detected = true;
        dev->removal_ctx.removal_time = time(NULL);
        dev->removal_ctx.pci_space_accessible = false;
        pthread_mutex_unlock(&dev->removal_ctx.detection_mutex);

        log_to_syslog(LOG_ERR, "Device %s (%s) removal detected!", dev->name, dev->pci_addr);
        return 1;
    }

    if (!was_present && now_present) {
        log_to_syslog(LOG_INFO, "Device %s (%s) re-inserted detected", dev->name, dev->pci_addr);
        dev->removal_ctx.removal_detected = false;
        dev->removal_ctx.pci_space_accessible = true;
        pthread_mutex_unlock(&dev->removal_ctx.detection_mutex);
        return 2;
    }

    pthread_mutex_unlock(&dev->removal_ctx.detection_mutex);
    return 0;
}

int start_removal_detection(nvme_manager_t *mgr) {
    if (!mgr) return -1;

    mgr->detection_thread_running = true;

    if (pthread_create(&mgr->detection_thread, NULL, removal_detection_thread, mgr) != 0) {
        log_to_syslog(LOG_ERR, "Failed to create removal detection thread");
        mgr->detection_thread_running = false;
        return -1;
    }

    pthread_detach(mgr->detection_thread);

    log_to_syslog(LOG_INFO, "Started device removal detection thread");
    return 0;
}

void stop_removal_detection(nvme_manager_t *mgr) {
    if (!mgr) return;

    mgr->detection_thread_running = false;

    log_to_syslog(LOG_INFO, "Stopped device removal detection thread");
}

static void* removal_detection_thread(void *arg) {
    nvme_manager_t *mgr = (nvme_manager_t *)arg;

    log_to_syslog(LOG_INFO, "Removal detection thread started");

    while (mgr->detection_thread_running) {
        pthread_mutex_lock(&mgr->mutex);

        for (int i = 0; i < mgr->device_count; i++) {
            nvme_device_t *dev = &mgr->devices[i];

            if (dev->state == DEVICE_STATE_REMOVING ||
                dev->state == DEVICE_STATE_REMOVED ||
                dev->state == DEVICE_STATE_REMOVAL_DETECTED) {
                continue;
            }

            int presence = check_device_presence(dev);

            if (presence == 1) {
                handle_device_removal_detected(mgr, dev);
            } else if (presence == 2) {
                handle_device_reinserted(mgr, dev);
            }

            if (dev->removal_ctx.removal_detected &&
                dev->state != DEVICE_STATE_READONLY &&
                dev->state != DEVICE_STATE_REMOVAL_DETECTED) {
                handle_device_removal_detected(mgr, dev);
            }
        }

        pthread_mutex_unlock(&mgr->mutex);

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = DEVICE_CHECK_INTERVAL_MS * 1000000;
        nanosleep(&ts, NULL);
    }

    log_to_syslog(LOG_INFO, "Removal detection thread exiting");
    return NULL;
}

int handle_device_removal_detected(nvme_manager_t *mgr, nvme_device_t *dev) {
    if (!mgr || !dev) return -1;

    pthread_mutex_lock(&dev->device_mutex);

    if (dev->state == DEVICE_STATE_REMOVAL_DETECTED ||
        dev->state == DEVICE_STATE_REMOVING) {
        pthread_mutex_unlock(&dev->device_mutex);
        return 0;
    }

    log_to_syslog(LOG_CRIT, "CRITICAL: Device %s removal detected during I/O!", dev->pci_addr);
    log_audit("device_removal", dev->pci_addr, "Device removal detected");

    dev->state = DEVICE_STATE_REMOVAL_DETECTED;
    dev->removal_ctx.removal_detected = true;
    dev->ctrlr_connected = false;

    pthread_mutex_unlock(&dev->device_mutex);

    int pending_count = cancel_all_pending_io(&dev->pending_ios, -ENXIO);
    log_to_syslog(LOG_ERR, "Cancelled %d pending I/Os for removed device %s",
                  pending_count, dev->pci_addr);

    if (strlen(dev->mount_point) > 0) {
        log_to_syslog(LOG_WARN, "Switching filesystem on %s to read-only", dev->mount_point);
        switch_filesystem_readonly(dev);
    }

    if (dev->is_raid_member) {
        log_to_syslog(LOG_WARN, "Device %s is RAID member, RAID may need recovery", dev->pci_addr);
    }

    log_to_syslog(LOG_CRIT, "Device %s is now in removal-detected state. "
                  "Application should handle -ENXIO errors gracefully.", dev->pci_addr);

    return 0;
}

int handle_device_reinserted(nvme_manager_t *mgr, nvme_device_t *dev) {
    if (!mgr || !dev) return -1;

    pthread_mutex_lock(&dev->device_mutex);

    if (!dev->removal_ctx.removal_detected) {
        pthread_mutex_unlock(&dev->device_mutex);
        return 0;
    }

    log_to_syslog(LOG_INFO, "Device %s re-inserted, attempting recovery", dev->pci_addr);

    dev->recovery_ctx.recovery_in_progress = true;
    dev->recovery_ctx.recovery_start_time = time(NULL);
    dev->recovery_ctx.recovery_attempts++;
    dev->state = DEVICE_STATE_RECOVERY_IN_PROGRESS;

    pthread_mutex_unlock(&dev->device_mutex);

    if (dev->recovery_ctx.force_rebuild) {
        log_to_syslog(LOG_INFO, "Force rebuild requested for %s", dev->pci_addr);
        if (rebuild_bdev(dev) != 0) {
            dev->recovery_ctx.recovery_success = false;
            dev->recovery_ctx.data_corrupted = true;
            dev->state = DEVICE_STATE_RECOVERY_FAILED;
            log_to_syslog(LOG_ERR, "Force rebuild failed for %s, recommend reformatting", dev->pci_addr);
            return -1;
        }
    } else {
        if (attempt_device_recovery(dev, false) != 0) {
            dev->recovery_ctx.recovery_success = false;
            dev->state = DEVICE_STATE_RECOVERY_FAILED;
            log_to_syslog(LOG_ERR, "Recovery failed for %s", dev->pci_addr);
            return -1;
        }
    }

    dev->recovery_ctx.recovery_success = true;
    dev->recovery_ctx.recovery_in_progress = false;
    dev->removal_ctx.removal_detected = false;
    dev->ctrlr_connected = true;
    dev->state = DEVICE_STATE_INITIALIZED;

    log_to_syslog(LOG_INFO, "Device %s recovered successfully", dev->pci_addr);
    log_audit("device_recovery", dev->pci_addr, "Device recovered successfully");

    return 0;
}
