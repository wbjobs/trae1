#ifndef VSHAPER_COMMON_H
#define VSHAPER_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <time.h>
#include <syslog.h>
#include <pthread.h>

#define VSHAPER_VERSION     "2.0.0"
#define MAX_IFNAME          16
#define MAX_RULES           16
#define MAX_PATH            512
#define MAX_CMD             1024
#define MAX_CRON_ENTRIES    16
#define MAX_HISTORY         32
#define MAX_HTTP_BUF        8192
#define MAX_HTTP_CLIENTS    8
#define DEFAULT_HTTP_PORT   8080
#define DEFAULT_TRANSITION_MS 5000
#define TUN_DEVICE_PATH     "/dev/net/tun"
#define SYSFS_NET_PATH      "/sys/class/net"

#define RATE_UNIT_MBIT      "mbit"
#define RATE_UNIT_KBIT      "kbit"
#define RATE_UNIT_MBYTES    "mbytes"
#define RATE_UNIT_KBYTES    "kbytes"

typedef struct {
    char    name[64];
    char    rate_limit[64];
    char    delay[64];
    char    loss[16];
    char    dup[16];
    char    reorder[16];
    int     burst_kbytes;
    int     latency_ms;
} rule_config_t;

typedef struct {
    char            ifname[MAX_IFNAME];
    char            ip[32];
    char            netmask[32];
    int             num_rules;
    rule_config_t   rules[MAX_RULES];
    int             daemon_mode;
    int             dry_run_mode;
    char            config_file[MAX_PATH];
    int             http_port;
    int             http_enabled;
    int             transition_ms;
    char            pcap_file[MAX_PATH];
    int             pcap_replay_mode;
    int             scheduler_enabled;
    int             history_enabled;
    int             syslog_enabled;
} app_config_t;

typedef struct {
    unsigned long   rx_packets;
    unsigned long   tx_packets;
    unsigned long   rx_bytes;
    unsigned long   tx_bytes;
    unsigned long   rx_dropped;
    unsigned long   tx_dropped;
    unsigned long   rx_errors;
    unsigned long   tx_errors;
    double          avg_delay_ms;
    unsigned long   delayed_packets;
    unsigned long   lost_packets;
    unsigned long   duplicated_packets;
    unsigned long   reordered_packets;
} stats_info_t;

typedef struct {
    char            name[64];
    char            cron_expr[128];
    rule_config_t   rule;
    int             priority;
    int             is_active;
    time_t          last_triggered;
} cron_task_t;

typedef struct {
    time_t          timestamp;
    rule_config_t   old_rule;
    rule_config_t   new_rule;
    char            reason[256];
    char            operator[64];
} rule_history_entry_t;

typedef struct {
    rule_history_entry_t entries[MAX_HISTORY];
    int                 count;
    int                 head;
    pthread_mutex_t     lock;
} rule_history_t;

extern volatile sig_atomic_t g_running;
extern app_config_t g_config;
extern rule_history_t g_rule_history;
extern rule_config_t g_current_rule;
extern pthread_mutex_t g_rule_lock;

void signal_handler(int sig);
int  parse_rate_value(const char *str, unsigned long *bytes_per_sec);
int  parse_delay_value(const char *str, unsigned long *microseconds);
int  parse_percentage(const char *str, double *probability);
int  rule_config_equal(const rule_config_t *a, const rule_config_t *b);
void rule_config_copy(rule_config_t *dst, const rule_config_t *src);

#endif
