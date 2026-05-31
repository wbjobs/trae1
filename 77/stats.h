#ifndef VSHAPER_STATS_H
#define VSHAPER_STATS_H

#include "common.h"

int  stats_collect(const char *ifname, stats_info_t *stats);
void stats_print(const char *ifname, const stats_info_t *stats);
int  stats_collect_all(const app_config_t *config, stats_info_t *stats);

#endif
