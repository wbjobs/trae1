#include "nvme_hotplug_cli.h"

static inline void strtrim(char *str) {
    if (!str) return;
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

int nvme_manager_init(nvme_manager_t *mgr) {
    memset(mgr, 0, sizeof(nvme_manager_t));
    pthread_mutex_init(&mgr->mutex, NULL);
    mgr->monitoring = false;
    mgr->uevent_sock = -1;
    return 0;
}

void nvme_manager_destroy(nvme_manager_t *mgr) {
    stop_removal_detection(mgr);
    stop_monitoring(mgr);

    for (int i = 0; i < mgr->device_count; i++) {
        nvme_device_t *dev = &mgr->devices[i];
        destroy_pending_io_queue(&dev->pending_ios);
        pthread_mutex_destroy(&dev->device_mutex);
        pthread_mutex_destroy(&dev->removal_ctx.detection_mutex);
    }

    pthread_mutex_destroy(&mgr->mutex);
}

int discover_nvme_devices(nvme_manager_t *mgr) {
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    pthread_mutex_lock(&mgr->mutex);

    dir = opendir(SYSFS_NVME_PATH);
    if (!dir) {
        pthread_mutex_unlock(&mgr->mutex);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL && count < MAX_NVME_DEVICES) {
        if (strncmp(entry->d_name, "nvme", 4) != 0) {
            continue;
        }

        nvme_device_t *dev = &mgr->devices[count];
        memset(dev, 0, sizeof(nvme_device_t));

        strncpy(dev->name, entry->d_name, MAX_NAME_LEN - 1);
        dev->state = DEVICE_STATE_DETECTED;

        char sysfs_path[MAX_PATH_LEN];
        snprintf(sysfs_path, sizeof(sysfs_path), "%s/%s", SYSFS_NVME_PATH, entry->d_name);

        char pci_addr_path[MAX_PATH_LEN];
        snprintf(pci_addr_path, sizeof(pci_addr_path), "%s/device/pci_address", sysfs_path);
        FILE *f = fopen(pci_addr_path, "r");
        if (f) {
            fscanf(f, "%s", dev->pci_addr);
            fclose(f);
        }

        snprintf(pci_addr_path, sizeof(pci_addr_path), "%s/device/serial", sysfs_path);
        f = fopen(pci_addr_path, "r");
        if (f) {
            fscanf(f, "%s", dev->serial);
            fclose(f);
        }

        snprintf(pci_addr_path, sizeof(pci_addr_path), "%s/device/model", sysfs_path);
        f = fopen(pci_addr_path, "r");
        if (f) {
            fgets(dev->model, MAX_NAME_LEN, f);
            strtrim(dev->model);
            fclose(f);
        }

        snprintf(pci_addr_path, sizeof(pci_addr_path), "%s/device/revision", sysfs_path);
        f = fopen(pci_addr_path, "r");
        if (f) {
            fscanf(f, "%s", dev->firmware_rev);
            fclose(f);
        }

        snprintf(pci_addr_path, sizeof(pci_addr_path), "%s/device/vendor", sysfs_path);
        f = fopen(pci_addr_path, "r");
        if (f) {
            fscanf(f, "%hu", &dev->vendor_id);
            fclose(f);
        }

        snprintf(pci_addr_path, sizeof(pci_addr_path), "%s/device/device", sysfs_path);
        f = fopen(pci_addr_path, "r");
        if (f) {
            fscanf(f, "%hu", &dev->device_id);
            fclose(f);
        }

        snprintf(pci_addr_path, sizeof(pci_addr_path), "%s/device/numa_node", sysfs_path);
        f = fopen(pci_addr_path, "r");
        if (f) {
            fclose(f);
        }

        snprintf(pci_addr_path, sizeof(pci_addr_path), "%s/device/size", sysfs_path);
        f = fopen(pci_addr_path, "r");
        if (f) {
            unsigned long long size_blocks;
            fscanf(f, "%llu", &size_blocks);
            dev->capacity = size_blocks * 512;
            fclose(f);
        }

        snprintf(pci_addr_path, sizeof(pci_addr_path), "%s/device/queue/nr_zones", sysfs_path);
        f = fopen(pci_addr_path, "r");
        if (f) {
            fclose(f);
        }

        get_device_health(dev);

        char link_speed_path[MAX_PATH_LEN];
        snprintf(link_speed_path, sizeof(link_speed_path), "%s/device/current_link_speed", sysfs_path);
        f = fopen(link_speed_path, "r");
        if (f) {
            char speed_str[32];
            fscanf(f, "%s", speed_str);
            if (strstr(speed_str, "8GT")) dev->pci_link_speed = 8;
            else if (strstr(speed_str, "16GT")) dev->pci_link_speed = 16;
            else if (strstr(speed_str, "5GT")) dev->pci_link_speed = 5;
            else dev->pci_link_speed = 0;
            fclose(f);
        }

        char link_width_path[MAX_PATH_LEN];
        snprintf(link_width_path, sizeof(link_width_path), "%s/device/current_link_width", sysfs_path);
        f = fopen(link_width_path, "r");
        if (f) {
            fscanf(f, "%u", &dev->pci_link_width);
            fclose(f);
        }

        dev->last_seen = time(NULL);

        pthread_mutex_init(&dev->device_mutex, NULL);
        init_pending_io_queue(&dev->pending_ios);
        memset(&dev->removal_ctx, 0, sizeof(removal_detection_t));
        memset(&dev->recovery_ctx, 0, sizeof(recovery_context_t));
        pthread_mutex_init(&dev->removal_ctx.detection_mutex, NULL);
        dev->ctrlr_fd = -1;
        dev->ctrlr_connected = false;
        dev->readonly_mode = false;

        count++;
    }

    closedir(dir);
    mgr->device_count = count;

    pthread_mutex_unlock(&mgr->mutex);

    return 0;
}

int handle_device_add(nvme_manager_t *mgr, const char *pci_addr) {
    pthread_mutex_lock(&mgr->mutex);

    nvme_device_t *dev = NULL;
    int dev_idx = -1;
    for (int i = 0; i < mgr->device_count; i++) {
        if (strcmp(mgr->devices[i].pci_addr, pci_addr) == 0) {
            dev = &mgr->devices[i];
            dev_idx = i;
            break;
        }
    }

    if (!dev) {
        if (mgr->device_count >= MAX_NVME_DEVICES) {
            pthread_mutex_unlock(&mgr->mutex);
            return -1;
        }
        dev = &mgr->devices[mgr->device_count];
        memset(dev, 0, sizeof(nvme_device_t));
        strncpy(dev->pci_addr, pci_addr, MAX_NAME_LEN - 1);
        dev_idx = mgr->device_count;
        mgr->device_count++;
    }

    dev->state = DEVICE_STATE_BINDING;
    pthread_mutex_unlock(&mgr->mutex);

    log_to_syslog(LOG_INFO, "Binding device %s to uio_pci_generic", pci_addr);
    if (bind_nvme_driver(pci_addr, "uio_pci_generic") != 0) {
        log_to_syslog(LOG_ERR, "Failed to bind device %s", pci_addr);
        dev->state = DEVICE_STATE_DETECTED;
        return -1;
    }

    pthread_mutex_lock(&mgr->mutex);
    dev->state = DEVICE_STATE_INITIALIZED;
    pthread_mutex_unlock(&mgr->mutex);

    log_to_syslog(LOG_INFO, "Initializing SPDK NVMe controller for %s", pci_addr);
    if (init_spdk_nvme_controller(dev) != 0) {
        log_to_syslog(LOG_ERR, "Failed to initialize SPDK controller for %s", pci_addr);
        dev->state = DEVICE_STATE_INITIALIZED;
    }

    get_device_health(dev);

    log_to_syslog(LOG_INFO, "Device %s added successfully", pci_addr);
    return 0;
}

int handle_device_remove(nvme_manager_t *mgr, const char *pci_addr) {
    pthread_mutex_lock(&mgr->mutex);

    nvme_device_t *dev = NULL;
    int dev_idx = -1;
    for (int i = 0; i < mgr->device_count; i++) {
        if (strcmp(mgr->devices[i].pci_addr, pci_addr) == 0) {
            dev = &mgr->devices[i];
            dev_idx = i;
            break;
        }
    }

    if (!dev) {
        pthread_mutex_unlock(&mgr->mutex);
        return -1;
    }

    dev->state = DEVICE_STATE_REMOVING;
    pthread_mutex_unlock(&mgr->mutex);

    if (strlen(dev->mount_point) > 0) {
        log_to_syslog(LOG_INFO, "Unmounting device %s from %s", pci_addr, dev->mount_point);
        unmount_device(dev);
    }

    if (dev->is_raid_member) {
        log_to_syslog(LOG_INFO, "Removing device %s from RAID %s", pci_addr, dev->raid_name);
    }

    cleanup_spdk_nvme_controller(dev);

    log_to_syslog(LOG_INFO, "Cleaning up device %s", pci_addr);
    unbind_nvme_driver(pci_addr);

    pthread_mutex_lock(&mgr->mutex);
    for (int i = dev_idx; i < mgr->device_count - 1; i++) {
        memcpy(&mgr->devices[i], &mgr->devices[i + 1], sizeof(nvme_device_t));
    }
    mgr->device_count--;
    pthread_mutex_unlock(&mgr->mutex);

    log_to_syslog(LOG_INFO, "Device %s removed successfully", pci_addr);
    return 0;
}

const char* device_state_str(device_state_t state) {
    switch (state) {
        case DEVICE_STATE_UNKNOWN: return "Unknown";
        case DEVICE_STATE_DETECTED: return "Detected";
        case DEVICE_STATE_BINDING: return "Binding";
        case DEVICE_STATE_INITIALIZED: return "Initialized";
        case DEVICE_STATE_FORMATTED: return "Formatted";
        case DEVICE_STATE_MOUNTED: return "Mounted";
        case DEVICE_STATE_RAID_MEMBER: return "RAID Member";
        case DEVICE_STATE_REMOVING: return "Removing";
        case DEVICE_STATE_REMOVED: return "Removed";
        case DEVICE_STATE_REMOVAL_DETECTED: return "Removal Detected";
        case DEVICE_STATE_RECOVERY_IN_PROGRESS: return "Recovery In Progress";
        case DEVICE_STATE_RECOVERY_FAILED: return "Recovery Failed";
        case DEVICE_STATE_READONLY: return "Read-Only";
        default: return "Invalid";
    }
}
