#include "nvme_hotplug_cli.h"

int get_device_health(nvme_device_t *dev) {
    if (dev == NULL) {
        return -1;
    }

    char temp_path[MAX_PATH_LEN];
    snprintf(temp_path, sizeof(temp_path), "/sys/class/nvme/%s/smart/temp", dev->name);
    FILE *f = fopen(temp_path, "r");
    if (f) {
        int temp;
        if (fscanf(f, "%d", &temp) == 1) {
            dev->temperature = temp;
        }
        fclose(f);
    }

    snprintf(temp_path, sizeof(temp_path), "/sys/class/nvme/%s/smart/power_on_hours", dev->name);
    f = fopen(temp_path, "r");
    if (f) {
        unsigned long long poh;
        if (fscanf(f, "%llu", &poh) == 1) {
            dev->power_on_hours = poh;
        }
        fclose(f);
    }

    snprintf(temp_path, sizeof(temp_path), "/sys/class/nvme/%s/smart/power_cycles", dev->name);
    f = fopen(temp_path, "r");
    if (f) {
        unsigned long long pc;
        if (fscanf(f, "%llu", &pc) == 1) {
            dev->power_cycles = pc;
        }
        fclose(f);
    }

    snprintf(temp_path, sizeof(temp_path), "/sys/class/nvme/%s/smart/data_units_read", dev->name);
    f = fopen(temp_path, "r");
    if (f) {
        unsigned long long dur;
        if (fscanf(f, "%llu", &dur) == 1) {
            dev->data_units_read = dur * 512 * 1000;
        }
        fclose(f);
    }

    snprintf(temp_path, sizeof(temp_path), "/sys/class/nvme/%s/smart/data_units_written", dev->name);
    f = fopen(temp_path, "r");
    if (f) {
        unsigned long long duw;
        if (fscanf(f, "%llu", &duw) == 1) {
            dev->data_units_written = duw * 512 * 1000;
        }
        fclose(f);
    }

    snprintf(temp_path, sizeof(temp_path), "/sys/class/nvme/%s/smart/percentage_used", dev->name);
    f = fopen(temp_path, "r");
    if (f) {
        int pu;
        if (fscanf(f, "%d", &pu) == 1) {
            dev->percent_used = pu;
        }
        fclose(f);
    }

    snprintf(temp_path, sizeof(temp_path), "/sys/class/nvme/%s/smart/media_errors", dev->name);
    f = fopen(temp_path, "r");
    if (f) {
        unsigned int me;
        if (fscanf(f, "%u", &me) == 1) {
            dev->media_errors = me;
        }
        fclose(f);
    }

    char smart_buffer[512];
    if (read_smart_data(dev, smart_buffer, sizeof(smart_buffer)) > 0) {
        parse_smart_log(smart_buffer, sizeof(smart_buffer), dev);
    }

    dev->last_seen = time(NULL);

    return 0;
}

void print_device_info(nvme_device_t *dev) {
    if (dev == NULL) return;

    printf("Device Information\n");
    printf("------------------\n");
    printf("Name: %s\n", dev->name);
    printf("PCI Address: %s\n", dev->pci_addr);
    printf("Serial: %s\n", dev->serial);
    printf("Model: %s\n", dev->model);
    printf("Firmware: %s\n", dev->firmware_rev);
    printf("Vendor ID: 0x%04x\n", dev->vendor_id);
    printf("Device ID: 0x%04x\n", dev->device_id);
    printf("Namespace ID: %u\n", dev->nsid);
    printf("Capacity: %llu bytes\n", (unsigned long long)dev->capacity);
    printf("Block Size: %u bytes\n", dev->block_size);
    printf("State: %s\n", device_state_str(dev->state));
    if (dev->fs_type != FS_TYPE_NONE) {
        printf("Filesystem: %s\n", filesystem_type_str(dev->fs_type));
    }
    if (strlen(dev->mount_point) > 0) {
        printf("Mount Point: %s\n", dev->mount_point);
    }
    printf("\n");
}

void print_device_health(nvme_device_t *dev) {
    if (dev == NULL) return;

    printf("Device Health Information\n");
    printf("--------------------------\n");
    printf("Temperature: %d C\n", dev->temperature);
    printf("Percentage Used: %d%%\n", dev->percent_used);
    printf("Data Units Read: %llu\n", (unsigned long long)dev->data_units_read);
    printf("Data Units Written: %llu\n", (unsigned long long)dev->data_units_written);
    printf("Power Cycles: %llu\n", (unsigned long long)dev->power_cycles);
    printf("Power On Hours: %llu\n", (unsigned long long)dev->power_on_hours);
    printf("Media Errors: %u\n", dev->media_errors);
    printf("PCIe Link Speed: Gen%d\n",
           dev->pci_link_speed == 8 ? 3 :
           dev->pci_link_speed == 16 ? 4 : 2);
    printf("PCIe Link Width: x%d\n", dev->pci_link_width);
    printf("Last Seen: %s", ctime(&dev->last_seen));
}

int check_device_health_status(nvme_device_t *dev) {
    if (dev == NULL) {
        return -1;
    }

    int issues = 0;

    if (dev->temperature > 80) {
        log_to_syslog(LOG_WARN, "Device %s temperature is high: %d C", dev->name, dev->temperature);
        issues++;
    }

    if (dev->temperature > 100) {
        log_to_syslog(LOG_ERR, "Device %s temperature is critical: %d C", dev->name, dev->temperature);
        issues++;
    }

    if (dev->percent_used >= 100) {
        log_to_syslog(LOG_ERR, "Device %s exceeded rated lifetime", dev->name);
        issues++;
    } else if (dev->percent_used >= 95) {
        log_to_syslog(LOG_WARN, "Device %s near end of lifetime: %d%% used", dev->name, dev->percent_used);
        issues++;
    }

    if (dev->media_errors > 0) {
        log_to_syslog(LOG_ERR, "Device %s has media errors: %u", dev->name, dev->media_errors);
        issues++;
    }

    return issues;
}

int refresh_all_device_health(nvme_manager_t *mgr) {
    pthread_mutex_lock(&mgr->mutex);

    for (int i = 0; i < mgr->device_count; i++) {
        nvme_device_t *dev = &mgr->devices[i];
        get_device_health(dev);
        check_device_health_status(dev);
    }

    pthread_mutex_unlock(&mgr->mutex);

    return 0;
}
