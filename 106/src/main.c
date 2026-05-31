#include "nvme_hotplug_cli.h"

static cli_config_t global_config;
static nvme_manager_t global_manager;
static volatile bool running = true;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        running = false;
        stop_removal_detection(&global_manager);
        stop_monitoring(&global_manager);
    }
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [OPTIONS] COMMAND [ARGS...]\n\n", prog_name);
    fprintf(stderr, "NVMe Hot-plug Management CLI Tool\n\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  list                           List all NVMe devices and health status\n");
    fprintf(stderr, "  monitor                        Monitor for hot-plug events (daemon mode)\n");
    fprintf(stderr, "  add <pci_addr>                 Initialize and mount a new NVMe device\n");
    fprintf(stderr, "  remove <pci_addr>             Remove and cleanup an NVMe device\n");
    fprintf(stderr, "  format <pci_addr> <ext4|xfs>  Format device with filesystem\n");
    fprintf(stderr, "  mount <pci_addr> <path>       Mount device to specified path\n");
    fprintf(stderr, "  unmount <pci_addr>            Unmount device\n");
    fprintf(stderr, "  raid-create <name> <raid0|raid1> <pci_addrs...>  Create RAID volume\n");
    fprintf(stderr, "  raid-destroy <name>           Destroy RAID volume\n");
    fprintf(stderr, "  smart-export <pci_addr> <file> Export SMART log to file\n");
    fprintf(stderr, "  predict <pci_addr>            Predict disk failure probability (0-100%%)\n");
    fprintf(stderr, "  migrate <source_pci> <target_pci>  Migrate data online to healthy disk\n");
    fprintf(stderr, "  force-recover <pci_addr>      Force rebuild bdev after removal (WARNING)\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -d, --daemon                   Run in daemon mode\n");
    fprintf(stderr, "  -v, --verbose                  Enable verbose output\n");
    fprintf(stderr, "  -m, --mount-base <path>        Base mount point directory (default: /mnt/nvme)\n");
    fprintf(stderr, "  -f, --fs <ext4|xfs>            Default filesystem type\n");
    fprintf(stderr, "  -l, --log-level <level>        Syslog log level (0-7)\n");
    fprintf(stderr, "  -h, --help                     Show this help message\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s list\n", prog_name);
    fprintf(stderr, "  %s monitor -d\n", prog_name);
    fprintf(stderr, "  %s add 0000:01:00.0\n", prog_name);
    fprintf(stderr, "  %s format 0000:01:00.0 xfs\n", prog_name);
    fprintf(stderr, "  %s mount 0000:01:00.0 /mnt/nvme0n1\n", prog_name);
    fprintf(stderr, "  %s raid-create nvme_raid0 raid0 0000:01:00.0 0000:02:00.0\n", prog_name);
    fprintf(stderr, "  %s smart-export 0000:01:00.0 /var/log/nvme_smart.log\n", prog_name);
    fprintf(stderr, "  %s predict 0000:01:00.0\n", prog_name);
    fprintf(stderr, "  %s migrate 0000:01:00.0 0000:02:00.0\n", prog_name);
    fprintf(stderr, "  %s force-recover 0000:01:00.0\n", prog_name);
}

int main(int argc, char *argv[]) {
    int ret;

    memset(&global_config, 0, sizeof(global_config));
    strcpy(global_config.mount_base, DEFAULT_MOUNT_BASE);
    global_config.default_fs = FS_TYPE_EXT4;
    global_config.log_level = LOG_INFO;
    global_config.verbose = false;
    global_config.daemon_mode = false;

    openlog("nvme-hotplug", LOG_PID, LOG_DAEMON);

    if (cli_parse_args(argc, argv, &global_config) != 0) {
        print_usage(argv[0]);
        return 1;
    }

    if (global_config.verbose) {
        setlogmask(LOG_UPTO(LOG_DEBUG));
    } else {
        setlogmask(LOG_UPTO(global_config.log_level));
    }

    if (nvme_manager_init(&global_manager) != 0) {
        fprintf(stderr, "Failed to initialize NVMe manager\n");
        return 1;
    }

    if (init_prediction_model(&global_manager) != 0) {
        fprintf(stderr, "Warning: Failed to initialize prediction model\n");
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (discover_nvme_devices(&global_manager) != 0) {
        fprintf(stderr, "Warning: Failed to discover NVMe devices\n");
    }

    if (argc < 2) {
        print_usage(argv[0]);
        nvme_manager_destroy(&global_manager);
        return 1;
    }

    const char *command = argv[optind];

    if (strcmp(command, "list") == 0) {
        ret = cli_list(&global_manager);
    } else if (strcmp(command, "monitor") == 0) {
        ret = cli_monitor(&global_manager);
    } else if (strcmp(command, "add") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "Error: PCI address required\n");
            ret = -1;
        } else {
            ret = cli_add_device(&global_manager, argv[optind + 1]);
        }
    } else if (strcmp(command, "remove") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "Error: PCI address required\n");
            ret = -1;
        } else {
            ret = cli_remove_device(&global_manager, argv[optind + 1]);
        }
    } else if (strcmp(command, "format") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "Error: PCI address and filesystem type required\n");
            ret = -1;
        } else {
            filesystem_type_t fs_type = FS_TYPE_NONE;
            if (strcmp(argv[optind + 2], "ext4") == 0) {
                fs_type = FS_TYPE_EXT4;
            } else if (strcmp(argv[optind + 2], "xfs") == 0) {
                fs_type = FS_TYPE_XFS;
            } else {
                fprintf(stderr, "Error: Invalid filesystem type. Use ext4 or xfs\n");
                ret = -1;
            }
            if (fs_type != FS_TYPE_NONE) {
                ret = cli_format(&global_manager, argv[optind + 1], fs_type);
            }
        }
    } else if (strcmp(command, "mount") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "Error: PCI address and mount point required\n");
            ret = -1;
        } else {
            ret = cli_mount(&global_manager, argv[optind + 1], argv[optind + 2]);
        }
    } else if (strcmp(command, "unmount") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "Error: PCI address required\n");
            ret = -1;
        } else {
            ret = cli_unmount(&global_manager, argv[optind + 1]);
        }
    } else if (strcmp(command, "raid-create") == 0) {
        if (optind + 3 >= argc) {
            fprintf(stderr, "Error: RAID name, level and at least one PCI address required\n");
            ret = -1;
        } else {
            raid_config_t raid_config;
            memset(&raid_config, 0, sizeof(raid_config));
            strncpy(raid_config.name, argv[optind + 1], MAX_NAME_LEN - 1);
            if (strcmp(argv[optind + 2], "raid0") == 0 || strcmp(argv[optind + 2], "raid0") == 0) {
                raid_config.level = RAID0;
            } else if (strcmp(argv[optind + 2], "raid1") == 0 || strcmp(argv[optind + 2], "raid1") == 0) {
                raid_config.level = RAID1;
            } else {
                fprintf(stderr, "Error: Invalid RAID level. Use raid0 or raid1\n");
                ret = -1;
            }
            if (raid_config.level != RAID_NONE) {
                int count = 0;
                for (int i = optind + 3; i < argc && count < MAX_NVME_DEVICES; i++) {
                    strncpy(raid_config.member_devices[count++], argv[i], MAX_NAME_LEN - 1);
                }
                raid_config.member_count = count;
                ret = cli_raid_create(&raid_config);
            }
        }
    } else if (strcmp(command, "raid-destroy") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "Error: RAID name required\n");
            ret = -1;
        } else {
            raid_config_t raid_config;
            memset(&raid_config, 0, sizeof(raid_config));
            strncpy(raid_config.name, argv[optind + 1], MAX_NAME_LEN - 1);
            ret = cli_raid_destroy(&raid_config);
        }
    } else if (strcmp(command, "smart-export") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "Error: PCI address and output file required\n");
            ret = -1;
        } else {
            ret = cli_smart_export(&global_manager, argv[optind + 1], argv[optind + 2]);
        }
    } else if (strcmp(command, "predict") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "Error: PCI address required\n");
            ret = -1;
        } else {
            ret = cli_predict(&global_manager, argv[optind + 1]);
        }
    } else if (strcmp(command, "migrate") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "Error: Source and target PCI addresses required\n");
            ret = -1;
        } else {
            ret = cli_migrate(&global_manager, argv[optind + 1], argv[optind + 2]);
        }
    } else if (strcmp(command, "force-recover") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "Error: PCI address required\n");
            ret = -1;
        } else {
            ret = cli_force_recover(&global_manager, argv[optind + 1]);
        }
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
        print_usage(argv[0]);
        ret = -1;
    }

    nvme_manager_destroy(&global_manager);
    closelog();
    return ret == 0 ? 0 : 1;
}
