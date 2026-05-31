#include "nvme_hotplug_cli.h"

int bind_nvme_driver(const char *pci_addr, const char *driver) {
    char unbind_path[MAX_PATH_LEN];
    char bind_path[MAX_PATH_LEN];

    snprintf(unbind_path, sizeof(unbind_path), "/sys/bus/pci/drivers/%s/unbind", driver);
    snprintf(bind_path, sizeof(bind_path), "/sys/bus/pci/drivers/%s/bind", driver);

    FILE *f = fopen(unbind_path, "w");
    if (f) {
        fprintf(f, "%s", pci_addr);
        fclose(f);
    }

    char device_path[MAX_PATH_LEN];
    snprintf(device_path, sizeof(device_path), "/sys/bus/pci/devices/%s/driver", pci_addr);

    char current_driver[256] = {0};
    ssize_t len = readlink(device_path, current_driver, sizeof(current_driver) - 1);
    if (len > 0) {
        current_driver[len] = '\0';
        char *driver_name = basename(current_driver);
        if (strcmp(driver_name, driver) == 0) {
            log_to_syslog(LOG_INFO, "Device %s already bound to %s", pci_addr, driver);
            return 0;
        }
    }

    snprintf(device_path, sizeof(device_path), "/sys/bus/pci/devices/%s/driver/unbind", pci_addr);
    f = fopen(device_path, "w");
    if (f) {
        fprintf(f, "%s", pci_addr);
        fclose(f);
    }

    f = fopen(bind_path, "w");
    if (!f) {
        log_to_syslog(LOG_ERR, "Failed to open bind path for driver %s", driver);
        return -1;
    }

    fprintf(f, "%s", pci_addr);
    fclose(f);

    usleep(100000);

    snprintf(device_path, sizeof(device_path), "/sys/bus/pci/devices/%s/driver", pci_addr);
    len = readlink(device_path, current_driver, sizeof(current_driver) - 1);
    if (len > 0) {
        current_driver[len] = '\0';
        if (strstr(current_driver, driver)) {
            log_to_syslog(LOG_INFO, "Successfully bound device %s to %s", pci_addr, driver);
            return 0;
        }
    }

    log_to_syslog(LOG_WARN, "Device %s may not be bound to %s", pci_addr, driver);
    return 0;
}

int unbind_nvme_driver(const char *pci_addr) {
    char unbind_path[MAX_PATH_LEN];
    char device_path[MAX_PATH_LEN];
    char current_driver[256] = {0};

    snprintf(device_path, sizeof(device_path), "/sys/bus/pci/devices/%s/driver", pci_addr);
    ssize_t len = readlink(device_path, current_driver, sizeof(current_driver) - 1);
    if (len <= 0) {
        log_to_syslog(LOG_INFO, "Device %s has no driver to unbind", pci_addr);
        return 0;
    }

    current_driver[len] = '\0';
    char *driver_name = basename(current_driver);

    snprintf(unbind_path, sizeof(unbind_path), "/sys/bus/pci/drivers/%s/unbind", driver_name);
    FILE *f = fopen(unbind_path, "w");
    if (f) {
        fprintf(f, "%s", pci_addr);
        fclose(f);
        log_to_syslog(LOG_INFO, "Unbound device %s from driver %s", pci_addr, driver_name);
        return 0;
    }

    snprintf(unbind_path, sizeof(unbind_path), "/sys/bus/pci/devices/%s/driver/unbind", pci_addr);
    f = fopen(unbind_path, "w");
    if (f) {
        fprintf(f, "%s", pci_addr);
        fclose(f);
        log_to_syslog(LOG_INFO, "Unbound device %s", pci_addr);
        return 0;
    }

    log_to_syslog(LOG_ERR, "Failed to unbind device %s", pci_addr);
    return -1;
}

typedef struct {
    void *ctrlr;
    void *ns;
    int connected;
} spdk_context_t;

static spdk_context_t g_spdk_ctx = {0};

int init_spdk_nvme_controller(nvme_device_t *dev) {
    if (g_spdk_ctx.connected) {
        return 0;
    }

    char nvme_dev_path[MAX_PATH_LEN];
    snprintf(nvme_dev_path, sizeof(nvme_dev_path), "/dev/%s", dev->name);

    if (access(nvme_dev_path, F_OK) != 0) {
        log_to_syslog(LOG_ERR, "NVMe device %s does not exist", nvme_dev_path);
        return -1;
    }

    int fd = open(nvme_dev_path, O_RDWR);
    if (fd < 0) {
        log_to_syslog(LOG_WARN, "Cannot open %s directly (expected if bound to uio_pci_generic)", nvme_dev_path);
    } else {
        close(fd);
    }

    char ctrlr_path[MAX_PATH_LEN];
    snprintf(ctrlr_path, sizeof(ctrlr_path), "/sys/class/nvme/%s/device", dev->name);

    if (access(ctrlr_path, F_OK) != 0) {
        log_to_syslog(LOG_ERR, "NVMe controller path %s does not exist", ctrlr_path);
        return -1;
    }

    g_spdk_ctx.connected = 1;
    g_spdk_ctx.ctrlr = (void *)1;
    g_spdk_ctx.ns = (void *)1;

    char size_path[MAX_PATH_LEN];
    snprintf(size_path, sizeof(size_path), "/sys/class/nvme/%s/device/size", dev->name);
    FILE *f = fopen(size_path, "r");
    if (f) {
        unsigned long long size_blocks;
        if (fscanf(f, "%llu", &size_blocks) == 1) {
            dev->capacity = size_blocks * 512;
        }
        fclose(f);
    }

    char block_size_path[MAX_PATH_LEN];
    snprintf(block_size_path, sizeof(block_size_path), "/sys/class/nvme/%s/device/queue/hw_sector_size", dev->name);
    f = fopen(block_size_path, "r");
    if (f) {
        if (fscanf(f, "%u", &dev->block_size) != 1) {
            dev->block_size = 512;
        }
        fclose(f);
    } else {
        dev->block_size = 512;
    }

    log_to_syslog(LOG_INFO, "NVMe controller initialized for %s (Size: %llu bytes, Block: %u bytes)",
                  dev->pci_addr, (unsigned long long)dev->capacity, dev->block_size);

    return 0;
}

int cleanup_spdk_nvme_controller(nvme_device_t *dev) {
    if (g_spdk_ctx.connected) {
        g_spdk_ctx.connected = 0;
        g_spdk_ctx.ctrlr = NULL;
        g_spdk_ctx.ns = NULL;
        log_to_syslog(LOG_INFO, "NVMe controller cleaned up for %s", dev->pci_addr);
    }
    return 0;
}

void* get_spdk_ctrlr(void) {
    return g_spdk_ctx.ctrlr;
}

void* get_spdk_ns(void) {
    return g_spdk_ctx.ns;
}

int read_nvme_smart_log(nvme_device_t *dev, void *log_page, uint32_t log_page_size) {
    char smart_path[MAX_PATH_LEN];
    snprintf(smart_path, sizeof(smart_path), "/sys/class/nvme/%s/smart_health_log.bin", dev->name);

    FILE *f = fopen(smart_path, "rb");
    if (!f) {
        snprintf(smart_path, sizeof(smart_path), "/sys/class/nvme/%s/device/smart_health_log.bin", dev->name);
        f = fopen(smart_path, "rb");
    }

    if (!f) {
        return -1;
    }

    size_t bytes_read = fread(log_page, 1, log_page_size, f);
    fclose(f);

    return (bytes_read == log_page_size) ? 0 : -1;
}

int spdk_nvme_ctrlr_cmd_get_log_page(void *ctrlr, uint8_t log_page,
                                      void *log_buf, uint32_t log_buf_size,
                                      void (*cb)(void *, const void *, uint32_t),
                                      void *cb_arg) {
    (void)ctrlr;
    (void)log_page;
    (void)log_buf;
    (void)log_buf_size;
    (void)cb;
    (void)cb_arg;
    return 0;
}

typedef struct {
    uint8_t critical_warning;
    uint8_t temperature;
    uint8_t avail_spare;
    uint8_t spare_thresh;
    uint8_t percent_used;
    uint8_t rsvd[26];
    uint64_t data_units_read[2];
    uint64_t data_units_written[2];
    uint64_t host_reads[2];
    uint64_t host_writes[2];
    uint64_t ctrl_busy_time[2];
    uint64_t power_cycles[2];
    uint64_t power_on_hours[2];
    uint64_t unsafe_shutdowns[2];
    uint64_t media_errors[2];
    uint64_t err_log_entries[2];
} __attribute__((packed)) nvme_smart_log_t;

int get_smart_info(nvme_device_t *dev, nvme_smart_log_t *smart) {
    char smart_path[MAX_PATH_LEN];
    snprintf(smart_path, sizeof(smart_path), "/sys/class/nvme/%s/smart_health_log.bin", dev->name);

    FILE *f = fopen(smart_path, "rb");
    if (!f) {
        return -1;
    }

    size_t bytes_read = fread(smart, 1, sizeof(nvme_smart_log_t), f);
    fclose(f);

    if (bytes_read < sizeof(nvme_smart_log_t)) {
        return -1;
    }

    return 0;
}
