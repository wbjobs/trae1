#include "nvme_hotplug_cli.h"

int mount_device(nvme_device_t *dev, const char *mount_point) {
    if (dev->state < DEVICE_STATE_FORMATTED) {
        log_to_syslog(LOG_ERR, "Device %s not formatted, cannot mount", dev->pci_addr);
        return -1;
    }

    char device_path[MAX_PATH_LEN];
    snprintf(device_path, sizeof(device_path), "/dev/%s", dev->name);

    if (access(device_path, F_OK) != 0) {
        log_to_syslog(LOG_ERR, "Device node %s does not exist", device_path);
        return -1;
    }

    struct stat st;
    if (stat(mount_point, &st) != 0) {
        if (mkdir_p(mount_point, 0755) != 0) {
            log_to_syslog(LOG_ERR, "Failed to create mount point %s", mount_point);
            return -1;
        }
    } else {
        if (!S_ISDIR(st.st_mode)) {
            log_to_syslog(LOG_ERR, "Mount point %s is not a directory", mount_point);
            return -1;
        }
    }

    DIR *dir = opendir(mount_point);
    if (dir) {
        struct dirent *entry;
        int count = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                count++;
                break;
            }
        }
        closedir(dir);
        if (count > 0) {
            log_to_syslog(LOG_ERR, "Mount point %s is not empty", mount_point);
            return -1;
        }
    }

    const char *fs_type_str = filesystem_type_str(dev->fs_type);
    char mount_opts[256] = "";
    if (dev->fs_type == FS_TYPE_EXT4) {
        snprintf(mount_opts, sizeof(mount_opts), "-o discard,noatime");
    } else if (dev->fs_type == FS_TYPE_XFS) {
        snprintf(mount_opts, sizeof(mount_opts), "-o noatime");
    }

    char cmd[MAX_PATH_LEN * 3];
    if (strlen(mount_opts) > 0) {
        snprintf(cmd, sizeof(cmd), "mount -t %s %s %s %s",
                 fs_type_str, mount_opts, device_path, mount_point);
    } else {
        snprintf(cmd, sizeof(cmd), "mount -t %s %s %s",
                 fs_type_str, device_path, mount_point);
    }

    log_to_syslog(LOG_INFO, "Mounting %s to %s with options: %s",
                  device_path, mount_point, mount_opts);

    int ret = system(cmd);
    if (ret != 0) {
        log_to_syslog(LOG_ERR, "Mount command failed for %s", device_path);
        return -1;
    }

    strncpy(dev->mount_point, mount_point, MAX_PATH_LEN - 1);
    dev->state = DEVICE_STATE_MOUNTED;

    log_to_syslog(LOG_INFO, "Successfully mounted %s to %s", device_path, mount_point);
    return 0;
}

int unmount_device(nvme_device_t *dev) {
    if (strlen(dev->mount_point) == 0) {
        log_to_syslog(LOG_INFO, "Device %s is not mounted", dev->pci_addr);
        return 0;
    }

    char cmd[MAX_PATH_LEN * 2];
    snprintf(cmd, sizeof(cmd), "umount -l %s", dev->mount_point);

    log_to_syslog(LOG_INFO, "Unmounting device %s from %s", dev->pci_addr, dev->mount_point);

    int ret = system(cmd);
    if (ret != 0) {
        log_to_syslog(LOG_ERR, "Unmount command failed for %s", dev->mount_point);
        return -1;
    }

    char *empty_marker = strchr(dev->mount_point, '\0');
    if (empty_marker) {
        size_t len = strlen(dev->mount_point);
        if (len > 0 && strstr(dev->mount_point, DEFAULT_MOUNT_BASE) == dev->mount_point) {
            rmdir(dev->mount_point);
        }
    }

    memset(dev->mount_point, 0, MAX_PATH_LEN);
    dev->state = DEVICE_STATE_FORMATTED;

    log_to_syslog(LOG_INFO, "Successfully unmounted device %s", dev->pci_addr);
    return 0;
}

int is_device_mounted(const char *pci_addr, char *mount_point, size_t mp_size) {
    FILE *fp = fopen(PROC_MOUNTS, "r");
    if (!fp) {
        return -1;
    }

    char line[1024];
    int found = 0;

    while (fgets(line, sizeof(line), fp)) {
        char dev_path[MAX_PATH_LEN];
        char mount_path[MAX_PATH_LEN];
        char fs_type[64];
        char options[256];

        sscanf(line, "%s %s %s %s", dev_path, mount_path, fs_type, options);

        if (strstr(dev_path, "nvme") != NULL) {
            FILE *f = fopen("/sys/class/nvme/device", "r");
            if (f) {
                char sysfs_path[MAX_PATH_LEN];
                if (fgets(sysfs_path, sizeof(sysfs_path), f)) {
                    if (strstr(sysfs_path, pci_addr)) {
                        strncpy(mount_point, mount_path, mp_size - 1);
                        found = 1;
                    }
                }
                fclose(f);
            }
        }
    }

    fclose(fp);
    return found ? 0 : -1;
}

static int mkdir_p(const char *path, mode_t mode) {
    char tmp[MAX_PATH_LEN];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}
