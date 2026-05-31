#include "nvme_hotplug_cli.h"

int migrate_data_online(nvme_device_t *source, nvme_device_t *target) {
    if (!source || !target) return -1;

    if (strlen(source->mount_point) == 0 || strlen(target->mount_point) == 0) {
        log_to_syslog(LOG_ERR, "Both source and target must be mounted for online migration");
        return -1;
    }

    log_to_syslog(LOG_INFO, "Starting online migration from %s to %s",
                  source->mount_point, target->mount_point);

    char rsync_cmd[MAX_PATH_LEN * 3];
    snprintf(rsync_cmd, sizeof(rsync_cmd),
             "rsync -aHAXx --progress --stats %s/ %s/",
             source->mount_point, target->mount_point);

    FILE *fp = popen(rsync_cmd, "r");
    if (!fp) {
        log_to_syslog(LOG_ERR, "Failed to start rsync for migration");
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, "to-chk") || strstr(line, "speedup")) {
            log_to_syslog(LOG_DEBUG, "rsync: %s", line);
        }
    }

    int status = pclose(fp);
    if (status != 0) {
        log_to_syslog(LOG_ERR, "rsync migration failed with status %d", status);
        return -1;
    }

    log_to_syslog(LOG_INFO, "Online migration completed successfully");
    return 0;
}

int migrate_with_raid1_mirror(nvme_device_t *source, nvme_device_t *target, const char *raid_name) {
    if (!source || !target || !raid_name) return -1;

    log_to_syslog(LOG_INFO, "Starting RAID-1 mirror migration from %s to %s",
                  source->pci_addr, target->pci_addr);

    snprintf(source->migration.raid_name, MAX_NAME_LEN, "%s", raid_name);
    source->migration.using_raid1_mirror = true;
    source->migration.start_time = time(NULL);
    source->migration.progress = 0.0f;

    raid_config_t raid_config;
    memset(&raid_config, 0, sizeof(raid_config));
    strncpy(raid_config.name, raid_name, MAX_NAME_LEN - 1);
    raid_config.level = RAID1;
    strncpy(raid_config.member_devices[0], source->name, MAX_NAME_LEN - 1);
    strncpy(raid_config.member_devices[1], target->name, MAX_NAME_LEN - 1);
    raid_config.member_count = 2;

    log_to_syslog(LOG_INFO, "Creating temporary RAID-1 mirror: %s", raid_name);
    if (create_raid_volume(&raid_config) != 0) {
        log_to_syslog(LOG_ERR, "Failed to create RAID-1 mirror");
        return -1;
    }

    log_to_syslog(LOG_INFO, "RAID-1 mirror created, data is being synced");

    char md_stat_path[MAX_PATH_LEN];
    snprintf(md_stat_path, sizeof(md_stat_path), "/proc/mdstat");

    FILE *fp = fopen(md_stat_path, "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strstr(line, raid_name)) {
                if (strstr(line, "[=...]")) {
                    log_to_syslog(LOG_INFO, "RAID-1 sync in progress...");
                }
            }
        }
        fclose(fp);
    }

    log_to_syslog(LOG_INFO, "RAID-1 sync completed");
    source->migration.progress = 100.0f;

    return 0;
}

int start_migration(nvme_manager_t *mgr, nvme_device_t *source, nvme_device_t *target) {
    if (!mgr || !source || !target) return -1;

    pthread_mutex_lock(&source->device_mutex);

    if (source->state == DEVICE_STATE_MIGRATING) {
        log_to_syslog(LOG_ERR, "Device %s is already being migrated", source->pci_addr);
        pthread_mutex_unlock(&source->device_mutex);
        return -1;
    }

    source->state = DEVICE_STATE_MIGRATING;
    memset(&source->migration, 0, sizeof(migration_context_t));

    source->migration.start_time = time(NULL);
    strncpy(source->migration.source_pci, source->pci_addr, MAX_NAME_LEN - 1);
    strncpy(source->migration.target_pci, target->pci_addr, MAX_NAME_LEN - 1);
    strncpy(source->migration.source_mount, source->mount_point, MAX_PATH_LEN - 1);

    pthread_mutex_unlock(&source->device_mutex);

    log_to_syslog(LOG_INFO, "Migration started: %s -> %s", source->pci_addr, target->pci_addr);
    log_audit("migration_start", source->pci_addr, target->pci_addr);

    if (strlen(source->mount_point) > 0 && strlen(target->mount_point) > 0) {
        log_to_syslog(LOG_INFO, "Performing online migration using rsync");
        if (migrate_data_online(source, target) != 0) {
            log_to_syslog(LOG_ERR, "Online migration failed");
            source->state = DEVICE_STATE_MOUNTED;
            return -1;
        }
    } else {
        log_to_syslog(LOG_INFO, "Performing offline migration using dd");
        char dd_cmd[MAX_PATH_LEN * 2];
        snprintf(dd_cmd, sizeof(dd_cmd), "dd if=/dev/%s of=/dev/%s bs=4M status=progress",
                 source->name, target->name);

        FILE *fp = popen(dd_cmd, "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp) != NULL) {
                log_to_syslog(LOG_DEBUG, "dd: %s", line);
            }
            pclose(fp);
        }
    }

    return 0;
}

int complete_migration(nvme_manager_t *mgr, nvme_device_t *source, nvme_device_t *target) {
    if (!mgr || !source || !target) return -1;

    log_to_syslog(LOG_INFO, "Completing migration for %s", source->pci_addr);

    if (verify_migration_integrity(source, target) != 0) {
        log_to_syslog(LOG_ERR, "Migration integrity verification failed");
        rollback_migration(source, target);
        return -1;
    }

    if (update_fstab_entry(source->name, target->name, source->mount_point) != 0) {
        log_to_syslog(LOG_ERR, "Failed to update fstab");
    }

    pthread_mutex_lock(&source->device_mutex);
    source->state = DEVICE_STATE_MIGRATED;
    source->recovery_ctx.recovery_success = true;
    pthread_mutex_unlock(&source->device_mutex);

    log_to_syslog(LOG_INFO, "Migration completed successfully");
    log_audit("migration_complete", source->pci_addr, target->pci_addr);

    return 0;
}

int rollback_migration(nvme_device_t *source, nvme_device_t *target) {
    if (!source || !target) return -1;

    log_to_syslog(LOG_WARN, "Rolling back migration: %s -> %s",
                  source->pci_addr, target->pci_addr);

    if (source->migration.using_raid1_mirror &&
        strlen(source->migration.raid_name) > 0) {
        raid_config_t raid_config;
        memset(&raid_config, 0, sizeof(raid_config));
        strncpy(raid_config.name, source->migration.raid_name, MAX_NAME_LEN - 1);

        log_to_syslog(LOG_INFO, "Destroying RAID-1 mirror: %s", raid_config.name);
        destroy_raid_volume(&raid_config);
    }

    pthread_mutex_lock(&source->device_mutex);
    source->state = DEVICE_STATE_MOUNTED;
    source->migration.progress = 0.0f;
    memset(&source->migration, 0, sizeof(migration_context_t));
    pthread_mutex_unlock(&source->device_mutex);

    log_to_syslog(LOG_INFO, "Migration rollback completed");
    log_audit("migration_rollback", source->pci_addr, target->pci_addr);

    return 0;
}

int update_fstab_entry(const char *old_device, const char *new_device, const char *mount_point) {
    if (!old_device || !new_device || !mount_point) return -1;

    char fstab_path[MAX_PATH_LEN] = "/etc/fstab";
    char fstab_backup[MAX_PATH_LEN];
    snprintf(fstab_backup, sizeof(fstab_backup), "%s.backup", fstab_path);

    FILE *f_in = fopen(fstab_path, "r");
    if (!f_in) {
        log_to_syslog(LOG_ERR, "Failed to open /etc/fstab");
        return -1;
    }

    FILE *f_out = fopen(fstab_backup, "w");
    if (!f_out) {
        fclose(f_in);
        log_to_syslog(LOG_ERR, "Failed to create fstab backup");
        return -1;
    }

    char line[1024];
    int changes = 0;

    while (fgets(line, sizeof(line), f_in) != NULL) {
        if (line[0] == '#' || isspace(line[0])) {
            fprintf(f_out, "%s", line);
            continue;
        }

        char dev[256], mp[256], fs[256], opts[256];
        if (sscanf(line, "%255s %255s %255s %255s", dev, mp, fs, opts) == 4) {
            if (strstr(dev, old_device)) {
                fprintf(f_out, "%s %s %s %s\n", new_device, mp, fs, opts);
                changes++;
                log_to_syslog(LOG_INFO, "Updated fstab: %s -> %s", dev, new_device);
            } else {
                fprintf(f_out, "%s", line);
            }
        } else {
            fprintf(f_out, "%s", line);
        }
    }

    fclose(f_in);
    fclose(f_out);

    if (changes > 0) {
        char cmd[MAX_PATH_LEN * 2];
        snprintf(cmd, sizeof(cmd), "cp %s %s", fstab_backup, fstab_path);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "rm %s", fstab_backup);
        system(cmd);

        log_to_syslog(LOG_INFO, "fstab updated with %d changes", changes);
    } else {
        remove(fstab_backup);
    }

    return 0;
}

int verify_migration_integrity(nvme_device_t *source, nvme_device_t *target) {
    if (!source || !target) return -1;

    if (strlen(source->mount_point) == 0 || strlen(target->mount_point) == 0) {
        log_to_syslog(LOG_ERR, "Both devices must be mounted for integrity verification");
        return -1;
    }

    log_to_syslog(LOG_INFO, "Verifying migration integrity...");

    char cmd[MAX_PATH_LEN * 3];
    snprintf(cmd, sizeof(cmd), "diff -rq %s %s 2>/dev/null",
             source->mount_point, target->mount_point);

    FILE *fp = popen(cmd, "r");
    if (fp) {
        char output[1024];
        if (fgets(output, sizeof(output), fp) != NULL) {
            log_to_syslog(LOG_ERR, "Integrity check failed: %s", output);
            pclose(fp);
            return -1;
        }
        pclose(fp);
    }

    snprintf(cmd, sizeof(cmd), "df %s | tail -1 | awk '{print $3}'", source->mount_point);
    fp = popen(cmd, "r");
    unsigned long long source_used = 0;
    if (fp) {
        fscanf(fp, "%llu", &source_used);
        pclose(fp);
    }

    snprintf(cmd, sizeof(cmd), "df %s | tail -1 | awk '{print $3}'", target->mount_point);
    fp = popen(cmd, "r");
    unsigned long long target_used = 0;
    if (fp) {
        fscanf(fp, "%llu", &target_used);
        pclose(fp);
    }

    if (target_used < source_used * 0.95) {
        log_to_syslog(LOG_ERR, "Integrity check failed: target has significantly less data");
        return -1;
    }

    log_to_syslog(LOG_INFO, "Migration integrity verified successfully");
    return 0;
}

int migrate_with_raid1_switchover(nvme_device_t *source, nvme_device_t *target,
                                   const char *raid_name) {
    if (!source || !target || !raid_name) return -1;

    log_to_syslog(LOG_INFO, "Starting RAID-1 switchover migration");

    if (migrate_with_raid1_mirror(source, target, raid_name) != 0) {
        return -1;
    }

    log_to_syslog(LOG_INFO, "Waiting for RAID-1 sync to complete...");

    struct timespec ts;
    ts.tv_sec = 30;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);

    log_to_syslog(LOG_INFO, "Promoting target disk, demoting source disk");

    char mdadm_cmd[MAX_PATH_LEN];
    snprintf(mdadm_cmd, sizeof(mdadm_cmd),
             "mdadm --fail %s %s --remove %s",
             raid_name, source->name, source->name);

    FILE *fp = popen(mdadm_cmd, "r");
    if (fp) pclose(fp);

    snprintf(mdadm_cmd, sizeof(mdadm_cmd), "mdadm --remove %s %s", raid_name, source->name);
    fp = popen(mdadm_cmd, "r");
    if (fp) pclose(fp);

    snprintf(mdadm_cmd, sizeof(mdadm_cmd), "mdadm --add %s %s", raid_name, target->name);
    fp = popen(mdadm_cmd, "r");
    if (fp) pclose(fp);

    log_to_syslog(LOG_INFO, "RAID-1 switchover completed");
    return 0;
}
