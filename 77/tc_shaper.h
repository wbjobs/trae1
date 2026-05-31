#ifndef VSHAPER_TC_SHAPER_H
#define VSHAPER_TC_SHAPER_H

#include "common.h"

#define IPTABLES_CHAIN_MAX  64
#define MAX_IPTABLES_RULES  8

typedef struct {
    char    ifname[MAX_IFNAME];
    char    rate_limit[64];
    char    delay[64];
    char    loss[16];
    char    dup[16];
    char    reorder[16];
    int     burst_kbytes;
    int     latency_ms;
    int     tbf_handle;
    int     netem_handle;
    int     is_applied;
    int     iptables_rule_applied;
    char    iptables_chain[IPTABLES_CHAIN_MAX];
    char    iptables_rule_spec[256];
} tc_shaper_t;

int  tc_shaper_init(tc_shaper_t *shaper, const char *ifname,
                    const rule_config_t *rule);
int  tc_shaper_apply(tc_shaper_t *shaper);
int  tc_shaper_remove(tc_shaper_t *shaper);
int  tc_shaper_show_stats(const tc_shaper_t *shaper, stats_info_t *stats);
void tc_shaper_destroy(tc_shaper_t *shaper);

int  tc_apply_token_bucket(const char *ifname, const char *rate,
                           int burst_kbytes, int latency_ms,
                           unsigned int parent_handle);
int  tc_apply_netem(const char *ifname, const char *delay,
                    const char *dup, const char *reorder,
                    unsigned int parent_handle);

int  iptables_apply_loss(const char *ifname, const char *loss_pct,
                         char *chain_name, char *rule_spec);
int  iptables_remove_loss(const char *chain_name, const char *rule_spec);
int  iptables_collect_stats(const char *chain_name,
                             const char *ifname,
                             unsigned long *lost_packets);

int  tc_run_cmd(const char *fmt, ...);

#endif
