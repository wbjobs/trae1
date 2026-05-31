#include "nvme_hotplug_cli.h"

int process_uevent(nvme_manager_t *mgr, const char *uevent_msg) {
    uevent_t uevent;
    memset(&uevent, 0, sizeof(uevent));
    uevent.timestamp = time(NULL);

    char *msg_copy = strdup(uevent_msg);
    if (!msg_copy) return -1;

    char *saveptr;
    char *line = strtok_r(msg_copy, "\n", &saveptr);
    while (line) {
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = line;
            char *value = eq + 1;

            if (strcmp(key, "ACTION") == 0) {
                strncpy(uevent.action, value, MAX_NAME_LEN - 1);
            } else if (strcmp(key, "DEVNAME") == 0) {
                strncpy(uevent.devname, value, MAX_NAME_LEN - 1);
            } else if (strcmp(key, "SUBSYSTEM") == 0) {
                strncpy(uevent.subsystem, value, MAX_NAME_LEN - 1);
            } else if (strcmp(key, "PCI_SLOT_NAME") == 0) {
                strncpy(uevent.pci_addr, value, MAX_NAME_LEN - 1);
            } else if (strcmp(key, "PCI_ADDR") == 0) {
                if (strlen(uevent.pci_addr) == 0) {
                    strncpy(uevent.pci_addr, value, MAX_NAME_LEN - 1);
                }
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(msg_copy);

    if (strcmp(uevent.subsystem, "nvme") != 0 && strcmp(uevent.subsystem, "pci") != 0) {
        return 0;
    }

    log_to_syslog(LOG_DEBUG, "UEVENT: action=%s, devname=%s, subsystem=%s, pci=%s",
                  uevent.action, uevent.devname, uevent.subsystem, uevent.pci_addr);

    if (strcmp(uevent.action, "add") == 0) {
        if (strlen(uevent.pci_addr) > 0) {
            log_to_syslog(LOG_INFO, "Hot-add detected for device %s", uevent.pci_addr);

            pthread_mutex_lock(&mgr->mutex);
            nvme_device_t *dev = NULL;
            for (int i = 0; i < mgr->device_count; i++) {
                if (strcmp(mgr->devices[i].pci_addr, uevent.pci_addr) == 0) {
                    dev = &mgr->devices[i];
                    break;
                }
            }
            pthread_mutex_unlock(&mgr->mutex);

            if (dev && dev->removal_ctx.removal_detected) {
                log_to_syslog(LOG_INFO, "Device %s re-inserted, initiating recovery", uevent.pci_addr);
                handle_device_reinserted(mgr, dev);
            } else {
                handle_device_add(mgr, uevent.pci_addr);
            }
        }
    } else if (strcmp(uevent.action, "remove") == 0) {
        if (strlen(uevent.pci_addr) > 0) {
            log_to_syslog(LOG_WARN, "Hot-remove detected for device %s", uevent.pci_addr);

            pthread_mutex_lock(&mgr->mutex);
            nvme_device_t *dev = NULL;
            for (int i = 0; i < mgr->device_count; i++) {
                if (strcmp(mgr->devices[i].pci_addr, uevent.pci_addr) == 0) {
                    dev = &mgr->devices[i];
                    break;
                }
            }
            pthread_mutex_unlock(&mgr->mutex);

            if (dev) {
                dev->removal_ctx.uevent_received = true;
                if (dev->state == DEVICE_STATE_MOUNTED ||
                    dev->state == DEVICE_STATE_INITIALIZED ||
                    dev->pending_ios.count > 0) {
                    log_to_syslog(LOG_CRIT, "CRITICAL: Device %s removed during active I/O!", uevent.pci_addr);
                    handle_device_removal_detected(mgr, dev);
                } else {
                    handle_device_remove(mgr, uevent.pci_addr);
                }
            } else {
                handle_device_remove(mgr, uevent.pci_addr);
            }
        }
    }

    return 0;
}

int start_monitoring(nvme_manager_t *mgr) {
    struct sockaddr_nl addr;
    int sock;

    sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (sock < 0) {
        log_to_syslog(LOG_ERR, "Failed to create UEVENT socket: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid();
    addr.nl_groups = 1;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_to_syslog(LOG_ERR, "Failed to bind UEVENT socket: %s", strerror(errno));
        close(sock);
        return -1;
    }

    mgr->uevent_sock = sock;
    mgr->monitoring = true;

    log_to_syslog(LOG_INFO, "Started monitoring NVMe hot-plug events");

    char buffer[UEVENT_BUF_SIZE];
    while (mgr->monitoring) {
        fd_set rfds;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(sock + 1, &rfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(sock, &rfds)) {
            int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (len > 0) {
                buffer[len] = '\0';
                process_uevent(mgr, buffer);
            }
        } else if (ret < 0) {
            if (errno != EINTR) {
                log_to_syslog(LOG_ERR, "select() error: %s", strerror(errno));
                break;
            }
        }
    }

    return 0;
}

void stop_monitoring(nvme_manager_t *mgr) {
    if (!mgr->monitoring) return;

    mgr->monitoring = false;

    if (mgr->uevent_sock >= 0) {
        close(mgr->uevent_sock);
        mgr->uevent_sock = -1;
    }

    log_to_syslog(LOG_INFO, "Stopped monitoring NVMe hot-plug events");
}

int monitor_sysfs_events(nvme_manager_t *mgr) {
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        return -1;
    }

    int wd = inotify_add_watch(inotify_fd, SYSFS_NVME_PATH, IN_CREATE | IN_DELETE);
    if (wd < 0) {
        close(inotify_fd);
        return -1;
    }

    char buffer[UEVENT_BUF_SIZE];
    while (mgr->monitoring) {
        fd_set rfds;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(inotify_fd, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(inotify_fd + 1, &rfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(inotify_fd, &rfds)) {
            int len = read(inotify_fd, buffer, sizeof(buffer));
            if (len > 0) {
                struct inotify_event *event = (struct inotify_event *)buffer;
                if (event->len > 0) {
                    if (event->mask & IN_CREATE) {
                        log_to_syslog(LOG_INFO, "sysfs: Device created: %s", event->name);
                        discover_nvme_devices(mgr);
                    } else if (event->mask & IN_DELETE) {
                        log_to_syslog(LOG_INFO, "sysfs: Device deleted: %s", event->name);
                        discover_nvme_devices(mgr);
                    }
                }
            }
        }
    }

    inotify_rm_watch(inotify_fd, wd);
    close(inotify_fd);
    return 0;
}
