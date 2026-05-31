#ifndef VSHAPER_TUN_DEV_H
#define VSHAPER_TUN_DEV_H

#include "common.h"

typedef struct {
    int         fd;
    char        ifname[MAX_IFNAME];
    char        ip[32];
    char        netmask[32];
    int         mtu;
    int         is_up;
    pid_t       owner_pid;
} tun_device_t;

int  tun_device_create(tun_device_t *dev, const char *ifname, const char *ip,
                       const char *netmask, int mtu);
int  tun_device_set_ip(tun_device_t *dev);
int  tun_device_up(tun_device_t *dev);
int  tun_device_down(tun_device_t *dev);
int  tun_device_set_mtu(tun_device_t *dev);
void tun_device_close(tun_device_t *dev);
int  tun_device_read(tun_device_t *dev, void *buf, size_t len);
int  tun_device_write(tun_device_t *dev, const void *buf, size_t len);

#endif
