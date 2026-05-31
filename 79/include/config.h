#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PATH_LEN 512
#define MAX_USB_DEVICES_CONFIG 10

typedef enum {
    CMD_FLASH = 0,
    CMD_GEN_KEY,
    CMD_SIGN_FIRMWARE,
    CMD_VERIFY_FIRMWARE
} command_t;

typedef struct {
    uint16_t    vid;
    uint16_t    pid;
    char        firmware_path[MAX_PATH_LEN];
    char        log_path[MAX_PATH_LEN];
    char        backup_dir[MAX_PATH_LEN];
    int         verify_only;
    int         no_backup;
    int         max_retries;
    int         max_devices;
    int         reconnect_enabled;
    int         reconnect_max_attempts;
    int         reconnect_interval_ms;

    int         sign_enabled;
    int         force;
    char        public_key_path[MAX_PATH_LEN];
    char        private_key_path[MAX_PATH_LEN];
    char        signature_path[MAX_PATH_LEN];
    char        manifest_path[MAX_PATH_LEN];
    uint16_t    vendor_id;
    uint16_t    class_id;
    uint64_t    expiration_days;

    command_t   command;
} config_t;

int  config_parse(int argc, char *argv[], config_t *cfg);
void config_print_usage(const char *prog_name);
void config_set_defaults(config_t *cfg);

#endif
