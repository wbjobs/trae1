#include "tun_dev.h"
#include <linux/rtnetlink.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if_arp.h>

#define NLA_ALIGN(len) (((len) + 3) & ~3)
#define NLMSG_ALIGN(len) (((len) + 3) & ~3)

static int netlink_open(void) {
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        fprintf(stderr, "[tun] netlink socket 创建失败: %s\n", strerror(errno));
        return -1;
    }
    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid();
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[tun] netlink bind 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static int netlink_send_recv(int fd, struct nlmsghdr *nlh) {
    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    nlh->nlmsg_pid = 0;
    nlh->nlmsg_seq = time(NULL);
    if (sendto(fd, nlh, nlh->nlmsg_len, 0, (struct sockaddr *)&addr,
               sizeof(addr)) < 0) {
        fprintf(stderr, "[tun] netlink send 失败: %s\n", strerror(errno));
        return -1;
    }
    char resp[8192];
    ssize_t rlen = recv(fd, resp, sizeof(resp), 0);
    if (rlen < 0) {
        fprintf(stderr, "[tun] netlink recv 失败: %s\n", strerror(errno));
        return -1;
    }
    for (struct nlmsghdr *nh = (struct nlmsghdr *)resp; NLMSG_OK(nh, rlen);
         nh = NLMSG_NEXT(nh, rlen)) {
        if (nh->nlmsg_type == NLMSG_ERROR) {
            struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nh);
            if (err->error != 0) {
                fprintf(stderr, "[tun] netlink 错误: %s\n",
                        strerror(-err->error));
                return -1;
            }
        }
    }
    return 0;
}

int tun_device_create(tun_device_t *dev, const char *ifname, const char *ip,
                       const char *netmask, int mtu) {
    if (!dev || !ifname || !ip || !netmask) return -1;

    memset(dev, 0, sizeof(*dev));
    strncpy(dev->ifname, ifname, MAX_IFNAME - 1);
    strncpy(dev->ip, ip, sizeof(dev->ip) - 1);
    strncpy(dev->netmask, netmask, sizeof(dev->netmask) - 1);
    dev->mtu = mtu > 0 ? mtu : 1500;

    int fd = open(TUN_DEVICE_PATH, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[tun] 无法打开 %s: %s\n", TUN_DEVICE_PATH,
                strerror(errno));
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        fprintf(stderr, "[tun] TUNSETIFF 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    dev->fd = fd;
    dev->owner_pid = getpid();
    printf("[tun] 虚拟网卡 %s 创建成功 (fd=%d)\n", dev->ifname, fd);
    return 0;
}

int tun_device_set_ip(tun_device_t *dev) {
    if (!dev || dev->fd < 0) return -1;

    int nl_fd = netlink_open();
    if (nl_fd < 0) return -1;

    unsigned int ifindex = if_nametoindex(dev->ifname);
    if (ifindex == 0) {
        fprintf(stderr, "[tun] 获取 ifindex 失败: %s\n", strerror(errno));
        close(nl_fd);
        return -1;
    }

    char req[4096];
    memset(req, 0, sizeof(req));

    struct ifinfomsg *ifi = (struct ifinfomsg *)(req + NLMSG_HDRLEN);
    ifi->ifi_family = AF_INET;
    ifi->ifi_index = (int)ifindex;
    ifi->ifi_change = IFF_UP;
    ifi->ifi_flags = IFF_UP | IFF_RUNNING;

    struct nlmsghdr *nlh = (struct nlmsghdr *)req;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*ifi));
    nlh->nlmsg_type = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;

    if (netlink_send_recv(nl_fd, nlh) < 0) {
        close(nl_fd);
        return -1;
    }

    memset(req, 0, sizeof(req));
    struct ifaddrmsg *ifa = (struct ifaddrmsg *)(req + NLMSG_HDRLEN);
    ifa->ifa_family = AF_INET;
    ifa->ifa_prefixlen = 24;
    ifa->ifa_scope = RT_SCOPE_UNIVERSE;
    ifa->ifa_index = (int)ifindex;

    int attr_len = NLMSG_LENGTH(sizeof(*ifa));
    struct rtattr *rta = (struct rtattr *)(req + attr_len);
    rta->rta_type = IFA_LOCAL;
    rta->rta_len = RTA_LENGTH(sizeof(struct in_addr));
    inet_pton(AF_INET, dev->ip, RTA_DATA(rta));
    attr_len += NLA_ALIGN(rta->rta_len);

    rta = (struct rtattr *)(req + attr_len);
    rta->rta_type = IFA_ADDRESS;
    rta->rta_len = RTA_LENGTH(sizeof(struct in_addr));
    inet_pton(AF_INET, dev->ip, RTA_DATA(rta));
    attr_len += NLA_ALIGN(rta->rta_len);

    nlh = (struct nlmsghdr *)req;
    nlh->nlmsg_len = attr_len;
    nlh->nlmsg_type = RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;

    int ret = netlink_send_recv(nl_fd, nlh);
    if (ret == 0) {
        printf("[tun] IP %s/%s 已配置到 %s\n", dev->ip, dev->netmask,
               dev->ifname);
    }
    close(nl_fd);
    return ret;
}

int tun_device_up(tun_device_t *dev) {
    if (!dev || dev->fd < 0) return -1;
    int nl_fd = netlink_open();
    if (nl_fd < 0) return -1;

    unsigned int ifindex = if_nametoindex(dev->ifname);
    if (ifindex == 0) { close(nl_fd); return -1; }

    char req[1024];
    memset(req, 0, sizeof(req));
    struct ifinfomsg *ifi = (struct ifinfomsg *)(req + NLMSG_HDRLEN);
    ifi->ifi_family = AF_INET;
    ifi->ifi_index = (int)ifindex;
    ifi->ifi_change = IFF_UP;
    ifi->ifi_flags = IFF_UP | IFF_RUNNING;

    struct nlmsghdr *nlh = (struct nlmsghdr *)req;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*ifi));
    nlh->nlmsg_type = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;

    int ret = netlink_send_recv(nl_fd, nlh);
    if (ret == 0) {
        dev->is_up = 1;
        printf("[tun] 接口 %s 已启用\n", dev->ifname);
    }
    close(nl_fd);
    return ret;
}

int tun_device_down(tun_device_t *dev) {
    if (!dev || dev->fd < 0) return -1;
    int nl_fd = netlink_open();
    if (nl_fd < 0) return -1;

    unsigned int ifindex = if_nametoindex(dev->ifname);
    if (ifindex == 0) { close(nl_fd); return -1; }

    char req[1024];
    memset(req, 0, sizeof(req));
    struct ifinfomsg *ifi = (struct ifinfomsg *)(req + NLMSG_HDRLEN);
    ifi->ifi_family = AF_INET;
    ifi->ifi_index = (int)ifindex;
    ifi->ifi_change = IFF_UP;
    ifi->ifi_flags = 0;

    struct nlmsghdr *nlh = (struct nlmsghdr *)req;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*ifi));
    nlh->nlmsg_type = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;

    int ret = netlink_send_recv(nl_fd, nlh);
    if (ret == 0) {
        dev->is_up = 0;
        printf("[tun] 接口 %s 已禁用\n", dev->ifname);
    }
    close(nl_fd);
    return ret;
}

int tun_device_set_mtu(tun_device_t *dev) {
    if (!dev || dev->fd < 0) return -1;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, dev->ifname, IFNAMSIZ - 1);
    ifr.ifr_mtu = dev->mtu;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    int ret = ioctl(sock, SIOCSIFMTU, &ifr);
    close(sock);
    if (ret == 0) {
        printf("[tun] MTU 设置为 %d\n", dev->mtu);
    }
    return ret;
}

void tun_device_close(tun_device_t *dev) {
    if (!dev) return;
    if (dev->fd >= 0) {
        tun_device_down(dev);
        close(dev->fd);
        dev->fd = -1;
        printf("[tun] 虚拟网卡 %s 已关闭\n", dev->ifname);
    }
}

int tun_device_read(tun_device_t *dev, void *buf, size_t len) {
    if (!dev || dev->fd < 0) return -1;
    return read(dev->fd, buf, len);
}

int tun_device_write(tun_device_t *dev, const void *buf, size_t len) {
    if (!dev || dev->fd < 0) return -1;
    return write(dev->fd, buf, len);
}
