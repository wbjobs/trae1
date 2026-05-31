#ifndef VSHAPER_CLI_PARSER_H
#define VSHAPER_CLI_PARSER_H

#include "common.h"

typedef struct {
    int     show_help;
    int     show_version;
    int     show_stats;
    int     show_history;
    int     daemon_mode;
    int     dry_run_mode;
    char    ifname[MAX_IFNAME];
    char    ip[32];
    char    netmask[32];
    char    rate_limit[64];
    char    delay[64];
    char    loss[16];
    char    dup[16];
    char    reorder[16];
    int     burst_kbytes;
    int     latency_ms;
    char    config_file[MAX_PATH];
    int     http_port;
    int     http_enabled;
    int     transition_ms;
    char    pcap_file[MAX_PATH];
    int     pcap_replay_mode;
    int     pcap_loop_count;
    int     pcap_speed_factor;
    int     scheduler_enabled;
    int     history_enabled;
    int     syslog_enabled;
    int     rollback_steps;
} cli_options_t;

void cli_print_help(const char *prog);
int  cli_parse_args(int argc, char *argv[], cli_options_t *opts,
                    app_config_t *config);

#endif
