#include "nvme_hotplug_cli.h"

int format_device(nvme_device_t *dev, filesystem_type_t fs_type) {
    if (dev->state < DEVICE_STATE_INITIALIZED) {
        log_to_syslog(LOG_ERR, "Device %s not initialized, cannot format", dev->pci_addr);
        return -1;
    }

    char device_path[MAX_PATH_LEN];
    snprintf(device_path, sizeof(device_path), "/dev/%s", dev->name);

    if (access(device_path, F_OK) != 0) {
        log_to_syslog(LOG_ERR, "Device node %s does not exist", device_path);
        return -1;
    }

    const char *fs_type_str = filesystem_type_str(fs_type);
    const char *mkfs_cmd;

    switch (fs_type) {
        case FS_TYPE_EXT4:
            mkfs_cmd = "/sbin/mkfs.ext4";
            break;
        case FS_TYPE_XFS:
            mkfs_cmd = "/sbin/mkfs.xfs";
            break;
        default:
            log_to_syslog(LOG_ERR, "Unsupported filesystem type");
            return -1;
    }

    char cmd[MAX_PATH_LEN * 2];
    if (fs_type == FS_TYPE_EXT4) {
        snprintf(cmd, sizeof(cmd), "%s -F -E lazy_itable_init=0,discard %s",
                 mkfs_cmd, device_path);
    } else if (fs_type == FS_TYPE_XFS) {
        snprintf(cmd, sizeof(cmd), "%s -f %s", mkfs_cmd, device_path);
    }

    log_to_syslog(LOG_INFO, "Formatting %s with %s", device_path, fs_type_str);

    int ret = system(cmd);
    if (ret != 0) {
        log_to_syslog(LOG_ERR, "Format command failed for %s", device_path);
        return -1;
    }

    dev->fs_type = fs_type;
    dev->state = DEVICE_STATE_FORMATTED;

    log_to_syslog(LOG_INFO, "Successfully formatted %s with %s", device_path, fs_type_str);
    return 0;
}

const char* filesystem_type_str(filesystem_type_t type) {
    switch (type) {
        case FS_TYPE_EXT4: return "ext4";
        case FS_TYPE_XFS: return "xfs";
        case FS_TYPE_NONE: return "none";
        default: return "unknown";
    }
}
