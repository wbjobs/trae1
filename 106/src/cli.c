#include "nvme_hotplug_cli.h"

int cli_parse_args(int argc, char *argv[], cli_config_t *config) {
    int c;
    int option_index = 0;

    static struct option long_options[] = {
        {"daemon", no_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},
        {"mount-base", required_argument, 0, 'm'},
        {"fs", required_argument, 0, 'f'},
        {"log-level", required_argument, 0, 'l'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "dvf:m:l:h", long_options, &option_index)) != -1) {
        switch (c) {
            case 'd':
                config->daemon_mode = true;
                break;
            case 'v':
                config->verbose = true;
                break;
            case 'm':
                strncpy(config->mount_base, optarg, MAX_PATH_LEN - 1);
                break;
            case 'f':
                if (strcmp(optarg, "ext4") == 0) {
                    config->default_fs = FS_TYPE_EXT4;
                } else if (strcmp(optarg, "xfs") == 0) {
                    config->default_fs = FS_TYPE_XFS;
                } else {
                    fprintf(stderr, "Invalid filesystem type: %s\n", optarg);
                    return -1;
                }
                break;
            case 'l':
                config->log_level = atoi(optarg);
                if (config->log_level < 0 || config->log_level > 7) {
                    fprintf(stderr, "Invalid log level: %s (must be 0-7)\n", optarg);
                    return -1;
                }
                break;
            case 'h':
                return 1;
            default:
                return -1;
        }
    }

    return 0;
}

int cli_predict(nvme_manager_t *mgr, const char *pci_addr) {
    nvme_device_t *dev = NULL;
    pthread_mutex_lock(&mgr->mutex);
    for (int i = 0; i < mgr->device_count; i++) {
        if (strcmp(mgr->devices[i].pci_addr, pci_addr) == 0) {
            dev = &mgr->devices[i];
            break;
        }
    }
    pthread_mutex_unlock(&mgr->mutex);

    if (!dev) {
        fprintf(stderr, "Device %s not found\n", pci_addr);
        return -1;
    }

    printf("\nAnalyzing disk failure prediction for %s...\n", pci_addr);

    prediction_result_t result;
    if (predict_disk_failure(mgr, dev, &result) != 0) {
        fprintf(stderr, "Failed to generate prediction for device %s\n", pci_addr);
        return -1;
    }

    print_prediction_result(dev);

    if (result.status == HEALTH_STATUS_CRITICAL) {
        printf("\n*** ACTION REQUIRED ***\n");
        printf("This disk has a %.1f%% probability of failure within %d days.\n",
               result.failure_probability, result.days_until_failure_estimate);
        printf("Recommendation: Migrate data to a healthy disk immediately.\n");
        printf("Usage: %s migrate %s <target_pci>\n", "nvme-hotplug", pci_addr);
    } else if (result.status == HEALTH_STATUS_WARNING) {
        printf("\n*** MONITOR CLOSELY ***\n");
        printf("This disk has a %.1f%% probability of failure within 60 days.\n",
               result.failure_probability);
        printf("Recommendation: Plan for migration within 30 days.\n");
    } else {
        printf("\nDisk health is good. Continue normal operation.\n");
    }

    log_audit("failure_prediction", pci_addr, result.recommendations);
    return 0;
}

int cli_migrate(nvme_manager_t *mgr, const char *source_pci, const char *target_pci) {
    nvme_device_t *source = NULL;
    nvme_device_t *target = NULL;

    pthread_mutex_lock(&mgr->mutex);
    for (int i = 0; i < mgr->device_count; i++) {
        if (strcmp(mgr->devices[i].pci_addr, source_pci) == 0) {
            source = &mgr->devices[i];
        }
        if (strcmp(mgr->devices[i].pci_addr, target_pci) == 0) {
            target = &mgr->devices[i];
        }
    }
    pthread_mutex_unlock(&mgr->mutex);

    if (!source) {
        fprintf(stderr, "Source device %s not found\n", source_pci);
        return -1;
    }

    if (!target) {
        fprintf(stderr, "Target device %s not found\n", target_pci);
        return -1;
    }

    if (source->state == DEVICE_STATE_MIGRATING) {
        fprintf(stderr, "Source device %s is already being migrated\n", source_pci);
        return -1;
    }

    printf("\nMigration Plan:\n");
    printf("===============\n");
    printf("Source: %s (%s)\n", source->name, source->pci_addr);
    printf("  Mount: %s\n", strlen(source->mount_point) > 0 ? source->mount_point : "Not mounted");
    printf("  Capacity: %.2f GB\n", source->capacity / 1000000000.0);
    printf("Target: %s (%s)\n", target->name, target->pci_addr);
    printf("  Mount: %s\n", strlen(target->mount_point) > 0 ? target->mount_point : "Not mounted");
    printf("  Capacity: %.2f GB\n", target->capacity / 1000000000.0);

    if (target->capacity < source->capacity) {
        fprintf(stderr, "ERROR: Target capacity (%.2f GB) is smaller than source (%.2f GB)\n",
                target->capacity / 1000000000.0, source->capacity / 1000000000.0);
        return -1;
    }

    if (source->prediction.failure_probability < FAILURE_PROBABILITY_THRESHOLD) {
        printf("\nNote: Source disk failure probability is only %.1f%% (threshold: %.1f%%)\n",
               source->prediction.failure_probability, FAILURE_PROBABILITY_THRESHOLD);
        printf("Proceed only if you have specific reasons to migrate.\n");
    }

    printf("\nThis operation will:\n");
    if (strlen(source->mount_point) > 0 && strlen(target->mount_point) > 0) {
        printf("1. Use rsync to copy data from %s to %s (online, no downtime)\n",
               source->mount_point, target->mount_point);
    } else {
        printf("1. Use dd to copy all data from /dev/%s to /dev/%s (offline)\n",
               source->name, target->name);
    }
    printf("2. Update /etc/fstab to use the new device\n");
    printf("3. Update mount point configuration\n");
    printf("\nContinue? (y/N): ", source->pci_addr);

    char confirm[8];
    if (fgets(confirm, sizeof(confirm), stdin) == NULL || confirm[0] != 'y') {
        printf("Migration cancelled.\n");
        return -1;
    }

    log_to_syslog(LOG_INFO, "Starting migration: %s -> %s", source_pci, target_pci);
    printf("\nStarting migration...\n");

    if (start_migration(mgr, source, target) != 0) {
        fprintf(stderr, "Migration failed\n");
        return -1;
    }

    printf("Migration in progress...\n");

    if (complete_migration(mgr, source, target) != 0) {
        fprintf(stderr, "Migration completion failed, rolling back\n");
        rollback_migration(source, target);
        return -1;
    }

    printf("\nMigration completed successfully!\n");
    printf("Source device %s is now in 'MIGRATED' state.\n", source_pci);
    printf("Target device %s now holds the data.\n", target_pci);
    printf("\nYou may now safely remove or repurpose the source device.\n");

    return 0;
}

int cli_list(nvme_manager_t *mgr) {
    if (discover_nvme_devices(mgr) != 0) {
        fprintf(stderr, "Failed to discover NVMe devices\n");
        return -1;
    }

    printf("%-20s %-18s %-40s %-10s %-12s %-10s %-8s %-10s\n",
           "Device", "PCI Address", "Model", "Capacity", "State", "Temp(C)", "Life(%)", "Mount");
    printf("%s\n", "-------------------------------------------------------------------------------------------------------------------");

    for (int i = 0; i < mgr->device_count; i++) {
        nvme_device_t *dev = &mgr->devices[i];
        char capacity_str[32];
        if (dev->capacity >= 1000000000000ULL) {
            snprintf(capacity_str, sizeof(capacity_str), "%.1f TB", dev->capacity / 1000000000000.0);
        } else if (dev->capacity >= 1000000000ULL) {
            snprintf(capacity_str, sizeof(capacity_str), "%.1f GB", dev->capacity / 1000000000.0);
        } else {
            snprintf(capacity_str, sizeof(capacity_str), "%llu B", (unsigned long long)dev->capacity);
        }

        printf("%-20s %-18s %-40s %-10s %-12s %-10d %-8d %-10s\n",
               dev->name,
               dev->pci_addr,
               dev->model,
               capacity_str,
               device_state_str(dev->state),
               dev->temperature,
               dev->percent_used,
               strlen(dev->mount_point) > 0 ? dev->mount_point : "N/A");
    }

    printf("\n");
    printf("PCIe Link: Speed: Gen%d, Width: x%d\n",
           mgr->device_count > 0 ? (mgr->devices[0].pci_link_speed == 8 ? 4 :
                                   mgr->devices[0].pci_link_speed == 16 ? 5 : 3) : 0,
           mgr->device_count > 0 ? mgr->devices[0].pci_link_width : 0);

    return 0;
}

int cli_monitor(nvme_manager_t *mgr) {
    log_to_syslog(LOG_INFO, "Starting NVMe hot-plug monitor");
    printf("NVMe hot-plug monitor started. Press Ctrl+C to stop.\n");

    if (start_monitoring(mgr) != 0) {
        fprintf(stderr, "Failed to start monitoring\n");
        return -1;
    }

    if (start_removal_detection(mgr) != 0) {
        fprintf(stderr, "Warning: Failed to start removal detection thread\n");
    }

    while (running) {
        sleep(1);
    }

    stop_removal_detection(mgr);
    stop_monitoring(mgr);
    log_to_syslog(LOG_INFO, "NVMe hot-plug monitor stopped");
    return 0;
}

int cli_add_device(nvme_manager_t *mgr, const char *pci_addr) {
    log_to_syslog(LOG_INFO, "Adding NVMe device: %s", pci_addr);
    int ret = handle_device_add(mgr, pci_addr);
    if (ret == 0) {
        printf("Successfully added device %s\n", pci_addr);
        log_audit("add", pci_addr, "Device added successfully");
    } else {
        fprintf(stderr, "Failed to add device %s\n", pci_addr);
        log_audit("add", pci_addr, "Device add failed");
    }
    return ret;
}

int cli_remove_device(nvme_manager_t *mgr, const char *pci_addr) {
    log_to_syslog(LOG_INFO, "Removing NVMe device: %s", pci_addr);
    int ret = handle_device_remove(mgr, pci_addr);
    if (ret == 0) {
        printf("Successfully removed device %s\n", pci_addr);
        log_audit("remove", pci_addr, "Device removed successfully");
    } else {
        fprintf(stderr, "Failed to remove device %s\n", pci_addr);
        log_audit("remove", pci_addr, "Device remove failed");
    }
    return ret;
}

int cli_format(nvme_manager_t *mgr, const char *pci_addr, filesystem_type_t fs_type) {
    nvme_device_t *dev = NULL;
    pthread_mutex_lock(&mgr->mutex);
    for (int i = 0; i < mgr->device_count; i++) {
        if (strcmp(mgr->devices[i].pci_addr, pci_addr) == 0) {
            dev = &mgr->devices[i];
            break;
        }
    }
    pthread_mutex_unlock(&mgr->mutex);

    if (!dev) {
        fprintf(stderr, "Device %s not found\n", pci_addr);
        return -1;
    }

    log_to_syslog(LOG_INFO, "Formatting device %s with %s", pci_addr, filesystem_type_str(fs_type));
    int ret = format_device(dev, fs_type);
    if (ret == 0) {
        printf("Successfully formatted device %s with %s\n", pci_addr, filesystem_type_str(fs_type));
        log_audit("format", pci_addr, filesystem_type_str(fs_type));
    } else {
        fprintf(stderr, "Failed to format device %s\n", pci_addr);
        log_audit("format", pci_addr, "Format failed");
    }
    return ret;
}

int cli_mount(nvme_manager_t *mgr, const char *pci_addr, const char *mount_point) {
    nvme_device_t *dev = NULL;
    pthread_mutex_lock(&mgr->mutex);
    for (int i = 0; i < mgr->device_count; i++) {
        if (strcmp(mgr->devices[i].pci_addr, pci_addr) == 0) {
            dev = &mgr->devices[i];
            break;
        }
    }
    pthread_mutex_unlock(&mgr->mutex);

    if (!dev) {
        fprintf(stderr, "Device %s not found\n", pci_addr);
        return -1;
    }

    log_to_syslog(LOG_INFO, "Mounting device %s to %s", pci_addr, mount_point);
    int ret = mount_device(dev, mount_point);
    if (ret == 0) {
        printf("Successfully mounted device %s to %s\n", pci_addr, mount_point);
        log_audit("mount", pci_addr, mount_point);
    } else {
        fprintf(stderr, "Failed to mount device %s\n", pci_addr);
        log_audit("mount", pci_addr, "Mount failed");
    }
    return ret;
}

int cli_unmount(nvme_manager_t *mgr, const char *pci_addr) {
    nvme_device_t *dev = NULL;
    pthread_mutex_lock(&mgr->mutex);
    for (int i = 0; i < mgr->device_count; i++) {
        if (strcmp(mgr->devices[i].pci_addr, pci_addr) == 0) {
            dev = &mgr->devices[i];
            break;
        }
    }
    pthread_mutex_unlock(&mgr->mutex);

    if (!dev) {
        fprintf(stderr, "Device %s not found\n", pci_addr);
        return -1;
    }

    log_to_syslog(LOG_INFO, "Unmounting device %s", pci_addr);
    int ret = unmount_device(dev);
    if (ret == 0) {
        printf("Successfully unmounted device %s\n", pci_addr);
        log_audit("unmount", pci_addr, "Device unmounted");
    } else {
        fprintf(stderr, "Failed to unmount device %s\n", pci_addr);
        log_audit("unmount", pci_addr, "Unmount failed");
    }
    return ret;
}

int cli_raid_create(raid_config_t *config) {
    log_to_syslog(LOG_INFO, "Creating RAID volume %s (level: %s, members: %d)",
                  config->name, raid_level_str(config->level), config->member_count);
    int ret = create_raid_volume(config);
    if (ret == 0) {
        printf("Successfully created RAID volume %s\n", config->name);
        log_audit("raid-create", config->name, raid_level_str(config->level));
    } else {
        fprintf(stderr, "Failed to create RAID volume %s\n", config->name);
        log_audit("raid-create", config->name, "RAID creation failed");
    }
    return ret;
}

int cli_raid_destroy(raid_config_t *config) {
    log_to_syslog(LOG_INFO, "Destroying RAID volume %s", config->name);
    int ret = destroy_raid_volume(config);
    if (ret == 0) {
        printf("Successfully destroyed RAID volume %s\n", config->name);
        log_audit("raid-destroy", config->name, "RAID destroyed");
    } else {
        fprintf(stderr, "Failed to destroy RAID volume %s\n", config->name);
        log_audit("raid-destroy", config->name, "RAID destroy failed");
    }
    return ret;
}

int cli_smart_export(nvme_manager_t *mgr, const char *pci_addr, const char *output_file) {
    nvme_device_t *dev = NULL;
    pthread_mutex_lock(&mgr->mutex);
    for (int i = 0; i < mgr->device_count; i++) {
        if (strcmp(mgr->devices[i].pci_addr, pci_addr) == 0) {
            dev = &mgr->devices[i];
            break;
        }
    }
    pthread_mutex_unlock(&mgr->mutex);

    if (!dev) {
        fprintf(stderr, "Device %s not found\n", pci_addr);
        return -1;
    }

    log_to_syslog(LOG_INFO, "Exporting SMART log for device %s to %s", pci_addr, output_file);
    int ret = export_smart_log(dev, output_file);
    if (ret == 0) {
        printf("Successfully exported SMART log to %s\n", output_file);
        log_audit("smart-export", pci_addr, output_file);
    } else {
        fprintf(stderr, "Failed to export SMART log\n");
        log_audit("smart-export", pci_addr, "SMART export failed");
    }
    return ret;
}

int cli_force_recover(nvme_manager_t *mgr, const char *pci_addr) {
    nvme_device_t *dev = NULL;
    pthread_mutex_lock(&mgr->mutex);
    for (int i = 0; i < mgr->device_count; i++) {
        if (strcmp(mgr->devices[i].pci_addr, pci_addr) == 0) {
            dev = &mgr->devices[i];
            break;
        }
    }
    pthread_mutex_unlock(&mgr->mutex);

    if (!dev) {
        fprintf(stderr, "Device %s not found\n", pci_addr);
        return -1;
    }

    if (dev->state != DEVICE_STATE_RECOVERY_FAILED &&
        dev->state != DEVICE_STATE_REMOVAL_DETECTED &&
        dev->state != DEVICE_STATE_READONLY) {
        fprintf(stderr, "Device %s is not in a failed/recovery state (current: %s)\n",
                pci_addr, device_state_str(dev->state));
        fprintf(stderr, "Use force-recover only for devices that need reconstruction\n");
        return -1;
    }

    log_to_syslog(LOG_WARN, "Force recovering device %s (WARNING: data may be lost)", pci_addr);
    printf("WARNING: Force recovery will rebuild bdev for %s\n", pci_addr);
    printf("This may result in data loss if filesystem is corrupted.\n");
    printf("Are you sure you want to continue? (y/N): ");

    char confirm[8];
    if (fgets(confirm, sizeof(confirm), stdin) == NULL || confirm[0] != 'y') {
        printf("Recovery cancelled.\n");
        return -1;
    }

    dev->recovery_ctx.force_rebuild = true;
    dev->recovery_ctx.recovery_attempts = 0;

    log_to_syslog(LOG_INFO, "Starting force recovery for device %s", pci_addr);

    if (rebuild_bdev(dev) != 0) {
        fprintf(stderr, "Force recovery failed for device %s\n", pci_addr);
        log_audit("force-recover", pci_addr, "Force recovery failed");
        dev->state = DEVICE_STATE_RECOVERY_FAILED;
        return -1;
    }

    printf("Force recovery successful for device %s\n", pci_addr);
    log_audit("force-recover", pci_addr, "Force recovery successful");

    if (dev->fs_type != FS_TYPE_NONE) {
        printf("NOTE: The device was formatted. You may need to:\n");
        printf("  1. Run fsck to check filesystem integrity\n");
        printf("  2. Remount the filesystem if check passes\n");
        printf("  3. Reformat if filesystem is corrupted\n");
    }

    return 0;
}
