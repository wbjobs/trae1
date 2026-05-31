#include "config.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_set_defaults(config_t *cfg)
{
    memset(cfg, 0, sizeof(config_t));
    cfg->vid                    = 0x1234;
    cfg->pid                    = 0x5678;
    cfg->verify_only            = 0;
    cfg->no_backup              = 0;
    cfg->max_retries            = 3;
    cfg->max_devices            = MAX_USB_DEVICES_CONFIG;
    cfg->reconnect_enabled      = 1;
    cfg->reconnect_max_attempts = 5;
    cfg->reconnect_interval_ms  = 1000;
    cfg->sign_enabled           = 1;
    cfg->force                  = 0;
    cfg->vendor_id              = 0x0001;
    cfg->class_id               = 0x0001;
    cfg->expiration_days        = 365;
    cfg->command                = CMD_FLASH;
    strcpy_s(cfg->log_path, sizeof(cfg->log_path), "flash_log.txt");
    strcpy_s(cfg->backup_dir, sizeof(cfg->backup_dir), "backups");
    strcpy_s(cfg->public_key_path, sizeof(cfg->public_key_path), "public_key.bin");
    strcpy_s(cfg->private_key_path, sizeof(cfg->private_key_path), "private_key.bin");
}

void config_print_usage(const char *prog_name)
{
    printf("USB Firmware Batch Flasher v3.0\n\n");
    printf("Usage: %s [command] [options] <firmware_file>\n\n", prog_name);
    printf("Commands:\n");
    printf("  flash (default)          Flash firmware to devices\n");
    printf("  --gen-key                Generate Ed25519 key pair\n");
    printf("  --sign-firmware          Sign firmware file\n");
    printf("  --verify-firmware        Verify firmware signature\n\n");
    printf("Flash Options:\n");
    printf("  --vid <VID>              USB vendor ID (default: 0x1234)\n");
    printf("  --pid <PID>              USB product ID (default: 0x5678)\n");
    printf("  --log <file>             Log file path (default: flash_log.txt)\n");
    printf("  --backup-dir <dir>       Backup directory (default: backups)\n");
    printf("  --verify-only            Only verify, no flashing\n");
    printf("  --no-backup              Skip firmware backup\n");
    printf("  --max-retries <N>        Max retries on failure (default: 3)\n");
    printf("  --max-devices <N>        Max concurrent devices (default: 10)\n");
    printf("  --no-reconnect           Disable auto-reconnect on disconnect\n");
    printf("  --reconnect-attempts <N> Max reconnect attempts (default: 5)\n");
    printf("  --reconnect-interval <N> Reconnect interval ms (default: 1000)\n");
    printf("  --no-sign                Skip signature verification\n");
    printf("  --force                  Force flash even if signature fails\n");
    printf("  --public-key <file>      Public key file (default: public_key.bin)\n");
    printf("  --signature <file>       Signature file (default: firmware.sig)\n");
    printf("  --manifest <file>        SUIT manifest file (default: firmware.manifest)\n\n");
    printf("Signing Options:\n");
    printf("  --private-key <file>     Private key file (default: private_key.bin)\n");
    printf("  --vendor-id <ID>         Vendor ID for manifest (default: 0x0001)\n");
    printf("  --class-id <ID>          Class ID for manifest (default: 0x0001)\n");
    printf("  --expiration-days <N>    Expiration days (default: 365, 0=never)\n");
    printf("  --help                   Show this help\n\n");
    printf("Supported firmware formats: .bin, .hex\n");
    printf("\nExamples:\n");
    printf("  %s flash firmware.bin\n", prog_name);
    printf("  %s --gen-key\n", prog_name);
    printf("  %s --sign-firmware firmware.bin\n", prog_name);
    printf("  %s --verify-firmware firmware.bin\n", prog_name);
    printf("  %s --vid 0x0483 --pid 0xDF11 firmware.bin\n", prog_name);
    printf("  %s --force firmware.bin\n", prog_name);
}

static int is_hex(const char *str)
{
    if (!str || !str[0]) return 0;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;
    for (int i = 0; str[i]; i++) {
        if (!((str[i] >= '0' && str[i] <= '9') ||
              (str[i] >= 'A' && str[i] <= 'F') ||
              (str[i] >= 'a' && str[i] <= 'f')))
            return 0;
    }
    return 1;
}

static uint16_t parse_hex16(const char *str)
{
    char *end;
    return (uint16_t)strtoul(str, &end, 16);
}

int config_parse(int argc, char *argv[], config_t *cfg)
{
    config_set_defaults(cfg);

    int firmware_found = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return -2;
        } else if (strcmp(argv[i], "--gen-key") == 0) {
            cfg->command = CMD_GEN_KEY;
        } else if (strcmp(argv[i], "--sign-firmware") == 0) {
            cfg->command = CMD_SIGN_FIRMWARE;
        } else if (strcmp(argv[i], "--verify-firmware") == 0) {
            cfg->command = CMD_VERIFY_FIRMWARE;
        } else if (strcmp(argv[i], "flash") == 0) {
            cfg->command = CMD_FLASH;
        } else if (strcmp(argv[i], "--vid") == 0 && i + 1 < argc) {
            i++;
            if (is_hex(argv[i])) {
                cfg->vid = parse_hex16(argv[i]);
            } else {
                fprintf(stderr, "Invalid VID: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            i++;
            if (is_hex(argv[i])) {
                cfg->pid = parse_hex16(argv[i]);
            } else {
                fprintf(stderr, "Invalid PID: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            i++;
            strncpy_s(cfg->log_path, sizeof(cfg->log_path), argv[i], _TRUNCATE);
        } else if (strcmp(argv[i], "--backup-dir") == 0 && i + 1 < argc) {
            i++;
            strncpy_s(cfg->backup_dir, sizeof(cfg->backup_dir), argv[i], _TRUNCATE);
        } else if (strcmp(argv[i], "--verify-only") == 0) {
            cfg->verify_only = 1;
        } else if (strcmp(argv[i], "--no-backup") == 0) {
            cfg->no_backup = 1;
        } else if (strcmp(argv[i], "--max-retries") == 0 && i + 1 < argc) {
            i++;
            cfg->max_retries = atoi(argv[i]);
            if (cfg->max_retries < 0) cfg->max_retries = 0;
            if (cfg->max_retries > 10) cfg->max_retries = 10;
        } else if (strcmp(argv[i], "--max-devices") == 0 && i + 1 < argc) {
            i++;
            cfg->max_devices = atoi(argv[i]);
            if (cfg->max_devices < 1) cfg->max_devices = 1;
            if (cfg->max_devices > MAX_USB_DEVICES_CONFIG) cfg->max_devices = MAX_USB_DEVICES_CONFIG;
        } else if (strcmp(argv[i], "--no-reconnect") == 0) {
            cfg->reconnect_enabled = 0;
        } else if (strcmp(argv[i], "--reconnect-attempts") == 0 && i + 1 < argc) {
            i++;
            cfg->reconnect_max_attempts = atoi(argv[i]);
            if (cfg->reconnect_max_attempts < 1) cfg->reconnect_max_attempts = 1;
            if (cfg->reconnect_max_attempts > 20) cfg->reconnect_max_attempts = 20;
        } else if (strcmp(argv[i], "--reconnect-interval") == 0 && i + 1 < argc) {
            i++;
            cfg->reconnect_interval_ms = atoi(argv[i]);
            if (cfg->reconnect_interval_ms < 100) cfg->reconnect_interval_ms = 100;
            if (cfg->reconnect_interval_ms > 10000) cfg->reconnect_interval_ms = 10000;
        } else if (strcmp(argv[i], "--no-sign") == 0) {
            cfg->sign_enabled = 0;
        } else if (strcmp(argv[i], "--force") == 0) {
            cfg->force = 1;
        } else if (strcmp(argv[i], "--public-key") == 0 && i + 1 < argc) {
            i++;
            strncpy_s(cfg->public_key_path, sizeof(cfg->public_key_path), argv[i], _TRUNCATE);
        } else if (strcmp(argv[i], "--private-key") == 0 && i + 1 < argc) {
            i++;
            strncpy_s(cfg->private_key_path, sizeof(cfg->private_key_path), argv[i], _TRUNCATE);
        } else if (strcmp(argv[i], "--signature") == 0 && i + 1 < argc) {
            i++;
            strncpy_s(cfg->signature_path, sizeof(cfg->signature_path), argv[i], _TRUNCATE);
        } else if (strcmp(argv[i], "--manifest") == 0 && i + 1 < argc) {
            i++;
            strncpy_s(cfg->manifest_path, sizeof(cfg->manifest_path), argv[i], _TRUNCATE);
        } else if (strcmp(argv[i], "--vendor-id") == 0 && i + 1 < argc) {
            i++;
            if (is_hex(argv[i])) {
                cfg->vendor_id = parse_hex16(argv[i]);
            } else {
                cfg->vendor_id = (uint16_t)atoi(argv[i]);
            }
        } else if (strcmp(argv[i], "--class-id") == 0 && i + 1 < argc) {
            i++;
            if (is_hex(argv[i])) {
                cfg->class_id = parse_hex16(argv[i]);
            } else {
                cfg->class_id = (uint16_t)atoi(argv[i]);
            }
        } else if (strcmp(argv[i], "--expiration-days") == 0 && i + 1 < argc) {
            i++;
            cfg->expiration_days = (uint64_t)atoll(argv[i]);
        } else if (argv[i][0] != '-') {
            strncpy_s(cfg->firmware_path, sizeof(cfg->firmware_path), argv[i], _TRUNCATE);
            firmware_found = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }

    if (cfg->command == CMD_FLASH && !firmware_found) {
        fprintf(stderr, "Error: No firmware file specified\n");
        return -1;
    }

    if (cfg->command == CMD_SIGN_FIRMWARE && !firmware_found) {
        fprintf(stderr, "Error: No firmware file specified for signing\n");
        return -1;
    }

    if (cfg->command == CMD_VERIFY_FIRMWARE && !firmware_found) {
        fprintf(stderr, "Error: No firmware file specified for verification\n");
        return -1;
    }

    if (firmware_found && cfg->command == CMD_FLASH) {
        if (_access(cfg->firmware_path, 0) != 0) {
            fprintf(stderr, "Error: Firmware file not found: %s\n", cfg->firmware_path);
            return -1;
        }
    }

    return 0;
}
