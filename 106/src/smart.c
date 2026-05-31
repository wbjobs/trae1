#include "nvme_hotplug_cli.h"

int export_smart_log(nvme_device_t *dev, const char *output_file) {
    if (dev == NULL || output_file == NULL) {
        return -1;
    }

    FILE *outf = fopen(output_file, "w");
    if (!outf) {
        log_to_syslog(LOG_ERR, "Failed to open output file %s: %s", output_file, strerror(errno));
        return -1;
    }

    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(outf, "NVMe SMART Log Export\n");
    fprintf(outf, "======================\n");
    fprintf(outf, "Device: %s\n", dev->name);
    fprintf(outf, "PCI Address: %s\n", dev->pci_addr);
    fprintf(outf, "Serial: %s\n", dev->serial);
    fprintf(outf, "Model: %s\n", dev->model);
    fprintf(outf, "Firmware Revision: %s\n", dev->firmware_rev);
    fprintf(outf, "Timestamp: %s\n", timestamp);
    fprintf(outf, "\n");

    fprintf(outf, "SMART/Health Information\n");
    fprintf(outf, "------------------------\n");
    fprintf(outf, "Temperature: %d Celsius\n", dev->temperature);
    fprintf(outf, "Percentage Used: %d%%\n", dev->percent_used);
    fprintf(outf, "Data Units Read: %llu\n", (unsigned long long)dev->data_units_read);
    fprintf(outf, "Data Units Written: %llu\n", (unsigned long long)dev->data_units_written);
    fprintf(outf, "Power Cycles: %llu\n", (unsigned long long)dev->power_cycles);
    fprintf(outf, "Power On Hours: %llu\n", (unsigned long long)dev->power_on_hours);
    fprintf(outf, "Media Errors: %u\n", dev->media_errors);
    fprintf(outf, "\n");

    fprintf(outf, "Device Capacity\n");
    fprintf(outf, "---------------\n");
    fprintf(outf, "Namespace Size: %llu bytes\n", (unsigned long long)dev->capacity);
    fprintf(outf, "Namespace Capacity: %llu bytes\n", (unsigned long long)dev->capacity);
    fprintf(outf, "Block Size: %u bytes\n", dev->block_size);
    fprintf(outf, "\n");

    fprintf(outf, "PCIe Information\n");
    fprintf(outf, "----------------\n");
    fprintf(outf, "Link Speed: Gen%d (%d GT/s)\n",
            dev->pci_link_speed == 8 ? 3 :
            dev->pci_link_speed == 16 ? 4 : 2,
            dev->pci_link_speed);
    fprintf(outf, "Link Width: x%d\n", dev->pci_link_width);
    fprintf(outf, "\n");

    char smart_data_path[MAX_PATH_LEN];
    snprintf(smart_data_path, sizeof(smart_data_path),
             "/sys/class/nvme/%s/smart_health_log.bin", dev->name);

    if (access(smart_data_path, R_OK) == 0) {
        FILE *smartf = fopen(smart_data_path, "rb");
        if (smartf) {
            fprintf(outf, "Raw SMART Log (hexdump):\n");
            fprintf(outf, "-------------------------\n");

            unsigned char buffer[256];
            size_t bytes_read;
            int offset = 0;

            while ((bytes_read = fread(buffer, 1, sizeof(buffer), smartf)) > 0) {
                fprintf(outf, "%04x: ", offset);
                for (size_t i = 0; i < bytes_read; i++) {
                    fprintf(outf, "%02x ", buffer[i]);
                    if ((i + 1) % 16 == 0) fprintf(outf, "\n");
                }
                offset += bytes_read;
            }

            fclose(smartf);
        }
    }

    fprintf(outf, "\nEnd of SMART Log Export\n");

    fclose(outf);

    log_to_syslog(LOG_INFO, "SMART log exported for device %s to %s", dev->name, output_file);

    return 0;
}

int read_smart_data(nvme_device_t *dev, void *buffer, size_t buffer_size) {
    if (dev == NULL || buffer == NULL) {
        return -1;
    }

    char smart_path[MAX_PATH_LEN];
    snprintf(smart_path, sizeof(smart_path), "/sys/class/nvme/%s/smart_health_log.bin", dev->name);

    FILE *f = fopen(smart_path, "rb");
    if (!f) {
        snprintf(smart_path, sizeof(smart_path), "/sys/class/nvme/%s/device/smart_health_log.bin", dev->name);
        f = fopen(smart_path, "rb");
    }

    if (!f) {
        log_to_syslog(LOG_ERR, "Failed to open SMART log for device %s", dev->name);
        return -1;
    }

    size_t bytes_read = fread(buffer, 1, buffer_size, f);
    fclose(f);

    return bytes_read;
}

int parse_smart_log(const void *smart_data, size_t data_size,
                    nvme_device_t *dev) {
    if (smart_data == NULL || dev == NULL || data_size < 512) {
        return -1;
    }

    const unsigned char *data = (const unsigned char *)smart_data;

    dev->temperature = data[1];
    if (dev->temperature == 0 || dev->temperature > 200) {
        dev->temperature = data[0];
    }

    dev->percent_used = data[3];

    uint64_t dur = 0;
    uint64_t duw = 0;
    for (int i = 0; i < 8; i++) {
        dur |= ((uint64_t)data[32 + i]) << (i * 8);
        duw |= ((uint64_t)data[40 + i]) << (i * 8);
    }
    dev->data_units_read = dur * 512;
    dev->data_units_written = duw * 512;

    uint64_t pc = 0;
    uint64_t poh = 0;
    for (int i = 0; i < 8; i++) {
        pc |= ((uint64_t)data[128 + i]) << (i * 8);
        poh |= ((uint64_t)data[144 + i]) << (i * 8);
    }
    dev->power_cycles = pc;
    dev->power_on_hours = poh;

    dev->media_errors = 0;
    for (int i = 0; i < 4; i++) {
        dev->media_errors |= ((uint32_t)data[188 + i]) << (i * 8);
    }

    return 0;
}
