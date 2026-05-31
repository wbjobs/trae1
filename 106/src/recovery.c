#include "nvme_hotplug_cli.h"

int switch_filesystem_readonly(nvme_device_t *dev) {
    if (!dev) return -1;

    if (strlen(dev->mount_point) == 0) {
        return 0;
    }

    log_to_syslog(LOG_WARN, "Switching device %s (%s) to read-only mode",
                  dev->name, dev->mount_point);

    if (remount_readonly(dev->mount_point) != 0) {
        log_to_syslog(LOG_ERR, "Failed to remount %s read-only, may need manual intervention",
                      dev->mount_point);
        dev->state = DEVICE_STATE_READONLY;
        return -1;
    }

    dev->readonly_mode = true;
    dev->state = DEVICE_STATE_READONLY;

    log_to_syslog(LOG_INFO, "Device %s is now read-only", dev->mount_point);
    log_audit("readonly_mode", dev->pci_addr, dev->mount_point);

    return 0;
}

int remount_readonly(const char *mount_point) {
    if (!mount_point) return -1;

    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) {
        return -1;
    }

    char line[1024];
    char found_mount[MAX_PATH_LEN] = {0};
    char found_dev[MAX_PATH_LEN] = {0};
    char found_fs[MAX_PATH_LEN] = {0};

    while (fgets(line, sizeof(line), fp)) {
        char dev[256], mp[256], fs[256], opts[256];
        sscanf(line, "%255s %255s %255s %255s", dev, mp, fs, opts);

        if (strcmp(mp, mount_point) == 0) {
            strncpy(found_mount, mp, MAX_PATH_LEN - 1);
            strncpy(found_dev, dev, MAX_PATH_LEN - 1);
            strncpy(found_fs, fs, MAX_PATH_LEN - 1);
            break;
        }
    }
    fclose(fp);

    if (strlen(found_mount) == 0) {
        log_to_syslog(LOG_ERR, "Mount point %s not found in /proc/mounts", mount_point);
        return -1;
    }

    char cmd[MAX_PATH_LEN * 2];
    snprintf(cmd, sizeof(cmd), "mount -o remount,ro %s", mount_point);

    int ret = system(cmd);
    if (ret != 0) {
        log_to_syslog(LOG_ERR, "Failed to remount %s read-only: %d", mount_point, ret);
        return -1;
    }

    log_to_syslog(LOG_INFO, "Successfully remounted %s read-only", mount_point);
    return 0;
}

int check_filesystem_corruption(nvme_device_t *dev) {
    if (!dev) return -1;

    if (strlen(dev->mount_point) == 0) {
        return 0;
    }

    char fsck_cmd[MAX_PATH_LEN];
    if (dev->fs_type == FS_TYPE_EXT4) {
        snprintf(fsck_cmd, sizeof(fsck_cmd), "/sbin/fsck.ext4 -n %s 2>&1", dev->mount_point);
    } else if (dev->fs_type == FS_TYPE_XFS) {
        snprintf(fsck_cmd, sizeof(fsck_cmd), "/sbin/xfs_repair -n %s 2>&1", dev->mount_point);
    } else {
        return 0;
    }

    FILE *fp = popen(fsck_cmd, "r");
    if (!fp) {
        return -1;
    }

    char output[4096];
    size_t output_len = 0;
    char line[512];

    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strlen(line);
        if (output_len + len < sizeof(output) - 1) {
            strcpy(output + output_len, line);
            output_len += len;
        }
    }

    int status = pclose(fp);

    if (status != 0) {
        log_to_syslog(LOG_ERR, "Filesystem corruption detected on %s", dev->mount_point);
        log_to_syslog(LOG_ERR, "fsck output: %s", output);
        dev->recovery_ctx.data_corrupted = true;
        return 1;
    }

    log_to_syslog(LOG_INFO, "Filesystem check passed for %s, no corruption detected", dev->mount_point);
    dev->recovery_ctx.data_corrupted = false;
    dev->recovery_ctx.data_integrity_checked = true;

    return 0;
}

int attempt_device_recovery(nvme_device_t *dev, bool force_rebuild) {
    if (!dev) return -1;

    log_to_syslog(LOG_INFO, "Attempting recovery for device %s (force=%d)", dev->pci_addr, force_rebuild);

    if (!is_device_present(dev->pci_addr)) {
        log_to_syslog(LOG_ERR, "Device %s not present, cannot recover", dev->pci_addr);
        return -1;
    }

    if (check_filesystem_corruption(dev) != 0) {
        log_to_syslog(LOG_ERR, "Filesystem corruption detected, recovery requires reformatting");
        dev->recovery_ctx.data_corrupted = true;
        return -1;
    }

    if (force_rebuild) {
        return rebuild_bdev(dev);
    }

    char device_path[MAX_PATH_LEN];
    snprintf(device_path, sizeof(device_path), "/dev/%s", dev->name);

    int fd = open(device_path, O_RDWR | O_EXCL);
    if (fd < 0) {
        if (errno == EBUSY) {
            log_to_syslog(LOG_ERR, "Device %s is busy, cannot recover", dev->pci_addr);
            return -1;
        }
        log_to_syslog(LOG_ERR, "Cannot open device %s: %s", device_path, strerror(errno));
        return -1;
    }

    unsigned char identify_data[4096];
    ssize_t ret = read(fd, identify_data, sizeof(identify_data));

    if (ret != sizeof(identify_data)) {
        log_to_syslog(LOG_ERR, "Failed to read identify data from %s", device_path);
        close(fd);
        return -1;
    }

    close(fd);

    if (bind_nvme_driver(dev->pci_addr, "uio_pci_generic") != 0) {
        log_to_syslog(LOG_ERR, "Failed to rebind driver for %s", dev->pci_addr);
        return -1;
    }

    dev->ctrlr_connected = true;
    dev->recovery_ctx.recovery_success = true;

    log_to_syslog(LOG_INFO, "Device %s recovered successfully", dev->pci_addr);

    return 0;
}

int rebuild_bdev(nvme_device_t *dev) {
    if (!dev) return -1;

    log_to_syslog(LOG_INFO, "Force rebuilding bdev for device %s", dev->pci_addr);

    cleanup_spdk_nvme_controller(dev);

    unbind_nvme_driver(dev->pci_addr);

    struct timespec ts;
    ts.tv_sec = 1;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);

    if (bind_nvme_driver(dev->pci_addr, "uio_pci_generic") != 0) {
        log_to_syslog(LOG_ERR, "Failed to rebind driver for %s during rebuild", dev->pci_addr);
        return -1;
    }

    if (init_spdk_nvme_controller(dev) != 0) {
        log_to_syslog(LOG_ERR, "Failed to reinitialize SPDK controller for %s", dev->pci_addr);
        return -1;
    }

    if (dev->fs_type != FS_TYPE_NONE) {
        log_to_syslog(LOG_WARN, "Device %s was formatted, data may be lost after rebuild", dev->pci_addr);
        log_to_syslog(LOG_WARN, "Recommend running filesystem check before remounting");
    }

    dev->recovery_ctx.recovery_success = true;
    dev->ctrlr_connected = true;

    log_to_syslog(LOG_INFO, "bdev rebuilt successfully for %s", dev->pci_addr);

    return 0;
}
