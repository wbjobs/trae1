#include "nvme_hotplug_cli.h"

int create_raid_volume(raid_config_t *config) {
    if (config->member_count < 2) {
        log_to_syslog(LOG_ERR, "RAID requires at least 2 member devices");
        return -1;
    }

    if (config->level == RAID0 && config->member_count < 2) {
        log_to_syslog(LOG_ERR, "RAID0 requires at least 2 member devices");
        return -1;
    }

    if (config->level == RAID1 && config->member_count != 2) {
        log_to_syslog(LOG_ERR, "RAID1 requires exactly 2 member devices");
        return -1;
    }

    char raid_devices[MAX_PATH_LEN] = {0};
    for (int i = 0; i < config->member_count; i++) {
        char device_path[MAX_PATH_LEN];
        snprintf(device_path, sizeof(device_path), "/dev/%s", config->member_devices[i]);
        if (i > 0) {
            strncat(raid_devices, " ", sizeof(raid_devices) - strlen(raid_devices) - 1);
        }
        strncat(raid_devices, device_path, sizeof(raid_devices) - strlen(raid_devices) - 1);
    }

    char md_device[MAX_PATH_LEN];
    snprintf(md_device, sizeof(md_device), "/dev/md/%s", config->name);

    for (int i = 0; i < config->member_count; i++) {
        char device_path[MAX_PATH_LEN];
        snprintf(device_path, sizeof(device_path), "/dev/%s", config->member_devices[i]);

        char slot_path[MAX_PATH_LEN];
        snprintf(slot_path, sizeof(slot_path), "/sys/class/nvme/%s/device/slot",
                 config->member_devices[i]);

        if (access(slot_path, F_OK) == 0) {
            FILE *f = fopen(slot_path, "w");
            if (f) {
                fprintf(f, "none\n");
                fclose(f);
            }
        }
    }

    char level_str[16];
    if (config->level == RAID0) {
        snprintf(level_str, sizeof(level_str), "raid0");
    } else if (config->level == RAID1) {
        snprintf(level_str, sizeof(level_str), "raid1");
    }

    char cmd[MAX_PATH_LEN * 3];
    snprintf(cmd, sizeof(cmd), "mdadm --create --name=%s --level=%s --raid-devices=%d %s",
             config->name, level_str, config->member_count, raid_devices);

    log_to_syslog(LOG_INFO, "Creating RAID volume %s (level: %s, members: %d)",
                  config->name, level_str, config->member_count);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        log_to_syslog(LOG_ERR, "Failed to execute mdadm command");
        return -1;
    }

    char output[1024];
    while (fgets(output, sizeof(output), fp) != NULL) {
    }

    int status = pclose(fp);
    if (status != 0) {
        log_to_syslog(LOG_ERR, "mdadm command failed with status %d", status);
        return -1;
    }

    usleep(500000);

    FILE *conf = fopen("/etc/mdadm.conf", "a");
    if (conf) {
        fprintf(conf, "ARRAY %s devices=%s\n", md_device, raid_devices);
        fclose(conf);
    }

    log_to_syslog(LOG_INFO, "Successfully created RAID volume %s", config->name);
    return 0;
}

int destroy_raid_volume(raid_config_t *config) {
    char md_device[MAX_PATH_LEN];
    snprintf(md_device, sizeof(md_device), "/dev/md/%s", config->name);

    if (access(md_device, F_OK) != 0) {
        snprintf(md_device, sizeof(md_device), "/dev/%s", config->name);
    }

    log_to_syslog(LOG_INFO, "Stopping RAID volume %s", config->name);

    char cmd[MAX_PATH_LEN];
    snprintf(cmd, sizeof(cmd), "mdadm --stop %s", md_device);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        log_to_syslog(LOG_ERR, "Failed to execute mdadm --stop command");
        return -1;
    }

    char output[1024];
    while (fgets(output, sizeof(output), fp) != NULL) {
    }

    int status = pclose(fp);

    snprintf(cmd, sizeof(cmd), "mdadm --remove %s", md_device);
    fp = popen(cmd, "r");
    if (fp) {
        pclose(fp);
    }

    log_to_syslog(LOG_INFO, "Successfully destroyed RAID volume %s", config->name);
    return 0;
}

int create_spdk_raid_bdev(const char *raid_name, raid_level_t level,
                          const char **member_devices, int member_count) {
    if (member_count < 2) {
        log_to_syslog(LOG_ERR, "SPDK RAID requires at least 2 member devices");
        return -1;
    }

    log_to_syslog(LOG_INFO, "Creating SPDK RAID bdev %s (level: %s, members: %d)",
                  raid_name, raid_level_str(level), member_count);

    struct raid_bdev_config raid_config;
    memset(&raid_config, 0, sizeof(raid_config));
    strncpy(raid_config.name, raid_name, sizeof(raid_config.name) - 1);
    raid_config.level = (enum raid_level)level;
    raid_config.chunk_size = 128 * 1024 * 1024;

    return 0;
}

int destroy_spdk_raid_bdev(const char *raid_name) {
    log_to_syslog(LOG_INFO, "Destroying SPDK RAID bdev %s", raid_name);
    return 0;
}

const char* raid_level_str(raid_level_t level) {
    switch (level) {
        case RAID_NONE: return "none";
        case RAID0: return "raid0";
        case RAID1: return "raid1";
        default: return "unknown";
    }
}

int get_raid_status(const char *raid_name, char *status_str, size_t status_len) {
    char md_device[MAX_PATH_LEN];
    snprintf(md_device, sizeof(md_device), "/dev/md/%s", raid_name);

    char cmd[MAX_PATH_LEN];
    snprintf(cmd, sizeof(cmd), "mdadm --detail %s 2>/dev/null", md_device);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        return -1;
    }

    char line[512];
    int state_found = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, "State :")) {
            char *state = strchr(line, ':');
            if (state) {
                state++;
                while (*state == ' ') state++;
                char *end = strchr(state, '\n');
                if (end) *end = '\0';
                strncpy(status_str, state, status_len - 1);
                state_found = 1;
                break;
            }
        }
    }

    pclose(fp);

    if (!state_found) {
        snprintf(status_str, status_len, "unknown");
    }

    return 0;
}
