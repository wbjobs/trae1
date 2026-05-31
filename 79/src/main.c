#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "compat.h"
#include "config.h"
#include "logger.h"
#include "progress.h"
#include "firmware.h"
#include "usb_handler.h"
#include "sign.h"
#include "suit.h"

#define MAX_DEVICES_INTERNAL 10
#define HOTPLUG_EVENT_REMOVED  0
#define HOTPLUG_EVENT_INSERTED 1

typedef struct {
    int             index;
    usb_device_t   *usb_dev;
    firmware_t     *fw;
    config_t       *cfg;
    int             result;
    char            sn[64];
    int             retry_count;
    int             reconnect_count;
    int             backup_done;
    int             erase_done;
    int             current_phase;
} flash_worker_t;

typedef enum {
    PHASE_IDLE = 0,
    PHASE_BACKUP,
    PHASE_ERASE,
    PHASE_WRITE,
    PHASE_VERIFY,
    PHASE_COMPLETE
} flash_phase_t;

static volatile int g_running = 1;
static pthread_t    g_hotplug_thread;
static volatile int g_hotplug_thread_running = 0;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
    printf("\n\n[!] Interrupted. Shutting down...\n");
}

static void progress_cb(int index, int op, uint32_t current, uint32_t total)
{
    progress_update(index, (operation_t)op, current, total);
}

static void hotplug_event_cb(int index, int event, void *user_data)
{
    flash_worker_t *workers = (flash_worker_t *)user_data;

    if (event == HOTPLUG_EVENT_REMOVED && index >= 0 && index < MAX_DEVICES_INTERNAL) {
        LOG_WARN("Hotplug: device %d removed", index);
        if (workers[index].usb_dev) {
            progress_set_disconnected(index);
            usb_set_state(workers[index].usb_dev, DEV_STATE_DISCONNECTED);
        }
    } else if (event == HOTPLUG_EVENT_INSERTED) {
        LOG_INFO("Hotplug: new device inserted");
    }
}

static void *hotplug_poll_thread(void *arg)
{
    (void)arg;
    g_hotplug_thread_running = 1;

    while (g_running && g_hotplug_thread_running) {
        usb_hotpoll_events();
#ifdef _WIN32
        Sleep(50);
#else
        usleep(50000);
#endif
    }

    return NULL;
}

static int create_backup_dir(const char *dir)
{
    if (_access(dir, 0) == 0) return 0;
    return mkdir(dir);
}

static int do_backup(flash_worker_t *worker)
{
    usb_device_t *dev = worker->usb_dev;
    config_t *cfg = worker->cfg;
    int index = worker->index;

    char backup_path[512];
    sprintf_s(backup_path, sizeof(backup_path), "%s/backup_%s_%08X.bin",
              cfg->backup_dir, worker->sn, (unsigned)time(NULL));

    uint8_t *backup_buf = (uint8_t *)malloc(MAX_FIRMWARE_SIZE);
    if (!backup_buf) {
        LOG_ERROR("Memory allocation failed for backup");
        return -1;
    }

    size_t backup_size = 0;
    LOG_INFO("Backing up firmware from device %d...", index);

    int ret = usb_backup(dev, backup_buf, MAX_FIRMWARE_SIZE, &backup_size, progress_cb);
    if (ret == 0) {
        firmware_save(backup_path, backup_buf, backup_size);
        logger_device(worker->sn, "BACKUP", backup_path);
        worker->backup_done = 1;
    } else {
        LOG_WARN("Backup failed for device %d", index);
        logger_device(worker->sn, "BACKUP_FAIL", "Could not read firmware");
    }

    free(backup_buf);
    return ret;
}

static int handle_disconnect_and_reconnect(flash_worker_t *worker)
{
    usb_device_t *dev = worker->usb_dev;
    config_t *cfg = worker->cfg;
    int index = worker->index;

    if (!cfg->reconnect_enabled) {
        LOG_ERROR("Device %d disconnected and reconnect disabled", index);
        return -1;
    }

    progress_set_reconnecting(index);
    logger_device(worker->sn, "DISCONNECTED", "Attempting reconnect");

    for (int attempt = 0; attempt < cfg->reconnect_max_attempts && g_running; attempt++) {
        worker->reconnect_count = attempt + 1;
        progress_set_reconnect(index, attempt + 1);

        int ret = usb_wait_reconnect(dev, 1, cfg->reconnect_interval_ms);
        if (ret == 0) {
            logger_device(worker->sn, "RECONNECTED",
                          "Reconnected successfully");
            LOG_INFO("Device %d reconnected after %d attempts", index, attempt + 1);
            return 0;
        }
    }

    return -1;
}

static int do_erase_with_reconnect(flash_worker_t *worker)
{
    usb_device_t *dev = worker->usb_dev;

    while (g_running) {
        int ret = usb_erase(dev, progress_cb);
        if (ret == 0) {
            worker->erase_done = 1;
            return 0;
        }

        if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
            if (handle_disconnect_and_reconnect(worker) < 0) {
                return -1;
            }
            continue;
        }

        return -1;
    }
    return -1;
}

static int do_write_with_reconnect(flash_worker_t *worker)
{
    usb_device_t *dev = worker->usb_dev;
    firmware_t *fw = worker->fw;
    int index = worker->index;

    uint32_t write_offset = usb_get_write_offset(dev);

    while (g_running) {
        int ret;
        if (dev->needs_resume && write_offset > 0) {
            LOG_INFO("Device %d: resuming write from offset %u", index, write_offset);
            ret = usb_write_resume(dev, fw->data, fw->size, write_offset, progress_cb);
            dev->needs_resume = 0;
        } else {
            ret = usb_write(dev, fw->data, fw->size, progress_cb);
        }

        if (ret == 0) {
            return 0;
        }

        write_offset = usb_get_write_offset(dev);
        worker->current_phase = PHASE_WRITE;

        if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
            if (handle_disconnect_and_reconnect(worker) < 0) {
                return -1;
            }
            continue;
        }

        return -1;
    }
    return -1;
}

static int do_verify_with_reconnect(flash_worker_t *worker)
{
    usb_device_t *dev = worker->usb_dev;
    firmware_t *fw = worker->fw;
    int index = worker->index;

    uint32_t verify_offset = usb_get_verify_offset(dev);

    while (g_running) {
        int ret;
        if (dev->needs_resume && verify_offset > 0) {
            LOG_INFO("Device %d: resuming verify from offset %u", index, verify_offset);
            ret = usb_verify_resume(dev, fw->data, fw->size, verify_offset, progress_cb);
            dev->needs_resume = 0;
        } else {
            ret = usb_verify(dev, fw->data, fw->size, progress_cb);
        }

        if (ret == 0) {
            return 0;
        }

        if (ret == -2) {
            LOG_ERROR("Device %d: verification mismatch", index);
            return -2;
        }

        verify_offset = usb_get_verify_offset(dev);
        worker->current_phase = PHASE_VERIFY;

        if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
            if (handle_disconnect_and_reconnect(worker) < 0) {
                return -1;
            }
            continue;
        }

        return -1;
    }
    return -1;
}

static void *flash_worker(void *arg)
{
    flash_worker_t *worker = (flash_worker_t *)arg;
    usb_device_t   *dev    = worker->usb_dev;
    firmware_t     *fw     = worker->fw;
    config_t       *cfg    = worker->cfg;
    int             index  = worker->index;

    worker->result = -1;
    worker->current_phase = PHASE_IDLE;

    if (usb_open_device(dev) < 0) {
        LOG_ERROR("Failed to open device %d", index);
        progress_set_failed(index);
        logger_device("UNKNOWN", "FAILED", "Open device failed");
        return NULL;
    }

    usb_get_sn(dev, worker->sn, sizeof(worker->sn));
    progress_set_device(index, worker->sn);

    logger_device(worker->sn, "CONNECTED", "Device opened successfully");

    if (!cfg->verify_only && !cfg->no_backup && !worker->backup_done) {
        worker->current_phase = PHASE_BACKUP;
        if (do_backup(worker) < 0) {
            if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
                if (handle_disconnect_and_reconnect(worker) < 0) {
                    progress_set_failed(index);
                    logger_device(worker->sn, "FAILED", "Reconnect failed during backup");
                    usb_close_device(dev);
                    return NULL;
                }
            }
        }
    }

    for (int retry = 0; retry <= cfg->max_retries && g_running; retry++) {
        worker->retry_count = retry;
        if (retry > 0) {
            progress_set_retry(index, retry);
            logger_device(worker->sn, "RETRY", "Retry attempt");
        }

        if (!cfg->verify_only) {
            if (!worker->erase_done) {
                worker->current_phase = PHASE_ERASE;
                if (do_erase_with_reconnect(worker) < 0) {
                    if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
                        progress_set_failed(index);
                        logger_device(worker->sn, "FAILED",
                                      "Reconnect failed during erase");
                        break;
                    }
                    continue;
                }
            }

            worker->current_phase = PHASE_WRITE;
            if (do_write_with_reconnect(worker) < 0) {
                if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
                    progress_set_failed(index);
                    logger_device(worker->sn, "FAILED",
                                  "Reconnect failed during write");
                    break;
                }
                continue;
            }
        }

        worker->current_phase = PHASE_VERIFY;
        int result = do_verify_with_reconnect(worker);
        if (result == 0) {
            progress_set_done(index);
            if (cfg->verify_only) {
                logger_device(worker->sn, "VERIFY_OK", "Verification passed");
            } else {
                logger_device(worker->sn, "SUCCESS",
                              "Flash and verify passed");
            }
            worker->result = 0;
            worker->current_phase = PHASE_COMPLETE;
            usb_close_device(dev);
            return NULL;
        } else if (result == -2) {
            logger_device(worker->sn, "VERIFY_FAIL",
                          "Verification mismatch");
            if (!cfg->verify_only) {
                worker->erase_done = 0;
                usb_set_write_offset(dev, 0);
                usb_set_verify_offset(dev, 0);
            }
        } else {
            if (usb_get_state(dev) != DEV_STATE_CONNECTED) {
                progress_set_failed(index);
                logger_device(worker->sn, "FAILED",
                              "Reconnect failed during verify");
                break;
            }
            logger_device(worker->sn, "ERROR",
                          "Flash operation failed");
        }
    }

    progress_set_failed(index);
    logger_device(worker->sn, "FAILED",
                  "Max retries exceeded or reconnect failed");
    worker->result = -1;
    usb_close_device(dev);
    return NULL;
}

static int cmd_gen_key(config_t *cfg)
{
    printf("Generating Ed25519 key pair...\n");

    ed25519_keypair_t kp;
    int ret = ed25519_generate_keypair(&kp);
    if (ret != SIGN_OK) {
        fprintf(stderr, "Error: Failed to generate key pair: %s\n", sign_strerror(ret));
        return 1;
    }

    ret = sign_save_keypair(&kp, cfg->public_key_path, cfg->private_key_path);
    if (ret != SIGN_OK) {
        fprintf(stderr, "Error: Failed to save key pair: %s\n", sign_strerror(ret));
        return 1;
    }

    printf("Key pair generated successfully:\n");
    printf("  Public key: %s\n", cfg->public_key_path);
    printf("  Private key: %s\n", cfg->private_key_path);
    printf("\nWARNING: Keep the private key secure!\n");

    return 0;
}

static int cmd_sign_firmware(config_t *cfg)
{
    printf("Signing firmware: %s\n", cfg->firmware_path);

    firmware_t *fw = firmware_load(cfg->firmware_path);
    if (!fw) {
        fprintf(stderr, "Error: Failed to load firmware\n");
        return 1;
    }

    uint8_t public_key[32], secret_key[64];
    int ret = sign_load_public_key(cfg->public_key_path, public_key);
    if (ret != SIGN_OK) {
        fprintf(stderr, "Error: Failed to load public key: %s\n", sign_strerror(ret));
        firmware_free(fw);
        return 1;
    }

    ret = sign_load_secret_key(cfg->private_key_path, secret_key);
    if (ret != SIGN_OK) {
        fprintf(stderr, "Error: Failed to load private key: %s\n", sign_strerror(ret));
        firmware_free(fw);
        return 1;
    }

    uint8_t signature[64];
    ret = ed25519_sign(fw->data, fw->size, secret_key, signature);
    if (ret != SIGN_OK) {
        fprintf(stderr, "Error: Failed to sign firmware: %s\n", sign_strerror(ret));
        firmware_free(fw);
        return 1;
    }

    char sig_path[MAX_PATH_LEN];
    if (cfg->signature_path[0]) {
        strcpy_s(sig_path, sizeof(sig_path), cfg->signature_path);
    } else {
        sprintf_s(sig_path, sizeof(sig_path), "%s.sig", cfg->firmware_path);
    }
    ret = sign_save_signature(signature, sig_path);
    if (ret != SIGN_OK) {
        fprintf(stderr, "Error: Failed to save signature: %s\n", sign_strerror(ret));
        firmware_free(fw);
        return 1;
    }

    suit_manifest_t manifest;
    uint64_t expiration = 0;
    if (cfg->expiration_days > 0) {
        expiration = (uint64_t)time(NULL) + cfg->expiration_days * 86400ULL;
    }

    ret = suit_create_manifest(&manifest, fw->data, fw->size,
                               public_key, secret_key,
                               cfg->vendor_id, cfg->class_id, expiration);
    if (ret != SUIT_OK) {
        fprintf(stderr, "Error: Failed to create manifest: %s\n", suit_strerror(ret));
        firmware_free(fw);
        return 1;
    }

    char manifest_path[MAX_PATH_LEN];
    if (cfg->manifest_path[0]) {
        strcpy_s(manifest_path, sizeof(manifest_path), cfg->manifest_path);
    } else {
        sprintf_s(manifest_path, sizeof(manifest_path), "%s.manifest", cfg->firmware_path);
    }
    ret = suit_save_manifest(&manifest, manifest_path);
    if (ret != SUIT_OK) {
        fprintf(stderr, "Error: Failed to save manifest: %s\n", suit_strerror(ret));
        firmware_free(fw);
        return 1;
    }

    printf("Firmware signed successfully:\n");
    printf("  Signature: %s\n", sig_path);
    printf("  Manifest: %s\n", manifest_path);
    printf("  Vendor ID: 0x%04X\n", cfg->vendor_id);
    printf("  Class ID: 0x%04X\n", cfg->class_id);
    if (cfg->expiration_days > 0) {
        printf("  Expiration: %llu days\n", (unsigned long long)cfg->expiration_days);
    } else {
        printf("  Expiration: never\n");
    }

    firmware_free(fw);
    return 0;
}

static int cmd_verify_firmware(config_t *cfg)
{
    printf("Verifying firmware: %s\n", cfg->firmware_path);

    firmware_t *fw = firmware_load(cfg->firmware_path);
    if (!fw) {
        fprintf(stderr, "Error: Failed to load firmware\n");
        return 1;
    }

    char manifest_path[MAX_PATH_LEN];
    if (cfg->manifest_path[0]) {
        strcpy_s(manifest_path, sizeof(manifest_path), cfg->manifest_path);
    } else {
        sprintf_s(manifest_path, sizeof(manifest_path), "%s.manifest", cfg->firmware_path);
    }

    suit_manifest_t manifest;
    int ret = suit_load_manifest(&manifest, manifest_path);
    if (ret != SUIT_OK) {
        fprintf(stderr, "Error: Failed to load manifest: %s\n", suit_strerror(ret));
        firmware_free(fw);
        return 1;
    }

    suit_vendor_t *vendors = NULL;
    int vendor_count = 0;
    int vendor_cap = 0;

    ret = suit_add_vendor(&vendors, &vendor_count, &vendor_cap,
                          "Default", manifest.vendor_id, NULL, 0, 0);
    if (ret != SUIT_OK) {
        fprintf(stderr, "Error: Failed to add vendor\n");
        firmware_free(fw);
        return 1;
    }

    ret = suit_verify_manifest(&manifest, fw->data, fw->size,
                               vendors, vendor_count);

    free(vendors);

    if (ret != SUIT_OK) {
        fprintf(stderr, "Error: Firmware verification failed: %s\n", suit_strerror(ret));
        switch (ret) {
            case SUIT_ERR_SIGNATURE:
                fprintf(stderr, "  Reason: Signature does not match firmware content\n");
                break;
            case SUIT_ERR_EXPIRED:
                fprintf(stderr, "  Reason: Firmware manifest has expired\n");
                break;
            case SUIT_ERR_UNAUTHORIZED:
                fprintf(stderr, "  Reason: Vendor not authorized or certificate expired\n");
                break;
            case SUIT_ERR_FORMAT:
                fprintf(stderr, "  Reason: Firmware size or CRC mismatch\n");
                break;
            default:
                fprintf(stderr, "  Reason: %s\n", suit_strerror(ret));
        }
        firmware_free(fw);
        return 1;
    }

    printf("Firmware verification successful!\n");
    printf("  Vendor ID: 0x%04X\n", manifest.vendor_id);
    printf("  Class ID: 0x%04X\n", manifest.class_id);
    printf("  Firmware size: %u bytes\n", manifest.firmware_size);
    printf("  Timestamp: %llu\n", (unsigned long long)manifest.timestamp);
    if (manifest.expiration > 0) {
        printf("  Expiration: %llu\n", (unsigned long long)manifest.expiration);
    }

    firmware_free(fw);
    return 0;
}

static int verify_firmware_signature(config_t *cfg, firmware_t *fw)
{
    if (!cfg->sign_enabled) {
        LOG_INFO("Signature verification disabled (--no-sign)");
        return 0;
    }

    printf("\n[*] Verifying firmware signature...\n");

    char manifest_path[MAX_PATH_LEN];
    if (cfg->manifest_path[0]) {
        strcpy_s(manifest_path, sizeof(manifest_path), cfg->manifest_path);
    } else {
        sprintf_s(manifest_path, sizeof(manifest_path), "%s.manifest", cfg->firmware_path);
    }

    if (_access(manifest_path, 0) != 0) {
        if (cfg->force) {
            LOG_WARN("Manifest file not found, but --force specified. Continuing...");
            return 0;
        }
        LOG_ERROR("Manifest file not found: %s", manifest_path);
        LOG_ERROR("Use --force to skip signature verification (for debugging)");
        return -1;
    }

    suit_manifest_t manifest;
    int ret = suit_load_manifest(&manifest, manifest_path);
    if (ret != SUIT_OK) {
        if (cfg->force) {
            LOG_WARN("Failed to load manifest, but --force specified. Continuing...");
            return 0;
        }
        LOG_ERROR("Failed to load manifest: %s", suit_strerror(ret));
        return -1;
    }

    suit_vendor_t *vendors = NULL;
    int vendor_count = 0;
    int vendor_cap = 0;

    suit_add_vendor(&vendors, &vendor_count, &vendor_cap,
                    "Default", manifest.vendor_id, NULL, 0, 0);

    ret = suit_verify_manifest(&manifest, fw->data, fw->size,
                               vendors, vendor_count);

    free(vendors);

    if (ret != SUIT_OK) {
        LOG_ERROR("Firmware signature verification failed: %s", suit_strerror(ret));

        switch (ret) {
            case SUIT_ERR_SIGNATURE:
                LOG_ERROR("  Reason: Signature does not match firmware content");
                LOG_ERROR("  The firmware file may have been tampered with");
                break;
            case SUIT_ERR_EXPIRED:
                LOG_ERROR("  Reason: Firmware manifest has expired");
                LOG_ERROR("  Please contact the vendor for an updated firmware");
                break;
            case SUIT_ERR_UNAUTHORIZED:
                LOG_ERROR("  Reason: Vendor not authorized or certificate expired");
                LOG_ERROR("  Vendor ID: 0x%04X", manifest.vendor_id);
                break;
            case SUIT_ERR_FORMAT:
                LOG_ERROR("  Reason: Firmware size or CRC mismatch with manifest");
                break;
            default:
                LOG_ERROR("  Reason: %s", suit_strerror(ret));
        }

        if (cfg->force) {
            LOG_WARN("--force specified, continuing despite signature failure...");
            return 0;
        }

        LOG_ERROR("Use --force to skip signature verification (for debugging only)");
        return -1;
    }

    printf("[+] Firmware signature verified successfully\n");
    printf("    Vendor ID: 0x%04X\n", manifest.vendor_id);
    printf("    Class ID: 0x%04X\n", manifest.class_id);
    return 0;
}

int main(int argc, char *argv[])
{
    config_t cfg;
    int ret = config_parse(argc, argv, &cfg);

    if (ret == -2) {
        config_print_usage(argv[0]);
        return 0;
    }
    if (ret < 0) {
        config_print_usage(argv[0]);
        return 1;
    }

    if (sign_init() < 0) {
        fprintf(stderr, "Error: Failed to initialize signing module\n");
        return 1;
    }

    if (cfg.command == CMD_GEN_KEY) {
        return cmd_gen_key(&cfg);
    }

    if (cfg.command == CMD_SIGN_FIRMWARE) {
        return cmd_sign_firmware(&cfg);
    }

    if (cfg.command == CMD_VERIFY_FIRMWARE) {
        return cmd_verify_firmware(&cfg);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (logger_init(cfg.log_path) < 0) {
        fprintf(stderr, "Warning: Could not open log file: %s\n", cfg.log_path);
    }

    LOG_INFO("=== USB Firmware Batch Flasher v3.0 ===");
    LOG_INFO("Firmware: %s", cfg.firmware_path);
    LOG_INFO("VID:PID: %04X:%04X", cfg.vid, cfg.pid);
    LOG_INFO("Mode: %s", cfg.verify_only ? "VERIFY ONLY" : "FLASH + VERIFY");
    LOG_INFO("Max retries: %d", cfg.max_retries);
    LOG_INFO("Max devices: %d", cfg.max_devices);
    LOG_INFO("Signature verification: %s", cfg.sign_enabled ? "enabled" : "disabled");
    if (cfg.force) LOG_INFO("Force mode: enabled (skip signature on failure)");
    LOG_INFO("Reconnect: %s (max %d attempts, %dms interval)",
             cfg.reconnect_enabled ? "enabled" : "disabled",
             cfg.reconnect_max_attempts, cfg.reconnect_interval_ms);

    if (!cfg.no_backup && !cfg.verify_only) {
        if (create_backup_dir(cfg.backup_dir) < 0) {
            LOG_WARN("Could not create backup directory: %s", cfg.backup_dir);
        }
    }

    firmware_t *fw = firmware_load(cfg.firmware_path);
    if (!fw) {
        LOG_ERROR("Failed to load firmware file");
        logger_close();
        return 1;
    }

    LOG_INFO("Firmware loaded: %zu bytes, CRC32: %08X",
             fw->size, firmware_crc32(fw->data, fw->size));

    if (verify_firmware_signature(&cfg, fw) < 0) {
        LOG_ERROR("Signature verification failed, aborting");
        firmware_free(fw);
        logger_close();
        return 1;
    }

    if (usb_init() < 0) {
        LOG_ERROR("USB initialization failed");
        firmware_free(fw);
        logger_close();
        return 1;
    }

    usb_device_t devices[MAX_DEVICES_INTERNAL];
    memset(devices, 0, sizeof(devices));

    int count = usb_enumerate(cfg.vid, cfg.pid, devices, cfg.max_devices);
    if (count <= 0) {
        LOG_ERROR("No USB devices found with VID=%04X PID=%04X", cfg.vid, cfg.pid);
        usb_cleanup();
        firmware_free(fw);
        logger_close();
        return 1;
    }

    LOG_INFO("Found %d device(s), starting %s...", count,
             cfg.verify_only ? "verification" : "flashing");

    flash_worker_t  workers[MAX_DEVICES_INTERNAL];
    pthread_t       threads[MAX_DEVICES_INTERNAL];
    int             success_count = 0;
    int             fail_count    = 0;

    memset(workers, 0, sizeof(workers));
    memset(threads, 0, sizeof(threads));

    if (cfg.reconnect_enabled) {
        usb_register_hotplug(cfg.vid, cfg.pid, hotplug_event_cb, workers);

        if (pthread_create(&g_hotplug_thread, NULL, hotplug_poll_thread, NULL) != 0) {
            LOG_ERROR("Failed to create hotplug poll thread");
        }
    }

    if (progress_init(count) < 0) {
        LOG_ERROR("Progress initialization failed");
        g_hotplug_thread_running = 0;
        usb_cleanup();
        firmware_free(fw);
        logger_close();
        return 1;
    }

    for (int i = 0; i < count && g_running; i++) {
        workers[i].index   = i;
        workers[i].usb_dev = &devices[i];
        workers[i].fw      = fw;
        workers[i].cfg     = &cfg;
        workers[i].result  = -1;

        if (pthread_create(&threads[i], NULL, flash_worker, &workers[i]) != 0) {
            LOG_ERROR("Failed to create thread for device %d", i);
            workers[i].result = -1;
        }
    }

    for (int i = 0; i < count; i++) {
        if (threads[i]) {
            pthread_join(threads[i], NULL);
        }
    }

    g_hotplug_thread_running = 0;
    if (g_hotplug_thread) {
        pthread_join(g_hotplug_thread, NULL);
    }

    printf("\n\n");
    printf("========================================\n");
    printf("  Batch Flash Summary\n");
    printf("========================================\n");

    for (int i = 0; i < count; i++) {
        const char *status = (workers[i].result == 0) ? "SUCCESS" : "FAILED";
        printf("  Device %d [%s]: %s", i, workers[i].sn, status);
        if (workers[i].reconnect_count > 0) {
            printf(" (reconnected %d times)", workers[i].reconnect_count);
        }
        printf("\n");
        if (workers[i].result == 0) {
            success_count++;
        } else {
            fail_count++;
        }
    }

    printf("----------------------------------------\n");
    printf("  Total: %d | Success: %d | Failed: %d\n",
           count, success_count, fail_count);
    printf("========================================\n");

    LOG_INFO("Batch complete: %d total, %d success, %d failed",
             count, success_count, fail_count);

    for (int i = 0; i < count; i++) {
        if (usb_get_state(&devices[i]) != DEV_STATE_DONE && devices[i].handle) {
            usb_close_device(&devices[i]);
        }
    }

    progress_cleanup();
    usb_unregister_hotplug();
    usb_cleanup();
    firmware_free(fw);
    sign_cleanup();
    logger_close();

    return (fail_count > 0) ? 1 : 0;
}
