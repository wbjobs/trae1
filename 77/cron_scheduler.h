#ifndef VSHAPER_CRON_H
#define VSHAPER_CRON_H

#include "common.h"

typedef struct {
    int minute;
    int hour;
    int day;
    int month;
    int weekday;
    int minute_wild;
    int hour_wild;
    int day_wild;
    int month_wild;
    int weekday_wild;
    int minute_range_start;
    int minute_range_end;
    int hour_range_start;
    int hour_range_end;
    int has_minute_range;
    int has_hour_range;
} cron_expr_t;

typedef struct {
    cron_task_t     tasks[MAX_CRON_ENTRIES];
    int             num_tasks;
    pthread_t       thread;
    int             running;
    pthread_mutex_t lock;
} cron_scheduler_t;

int  cron_parse_expression(const char *expr, cron_expr_t *parsed);
int  cron_expr_matches(const cron_expr_t *expr, const struct tm *tm);
int  cron_scheduler_init(cron_scheduler_t *sched);
int  cron_scheduler_add_task(cron_scheduler_t *sched, const char *name,
                             const char *cron_expr, const rule_config_t *rule,
                             int priority);
void cron_scheduler_start(cron_scheduler_t *sched);
void cron_scheduler_stop(cron_scheduler_t *sched);
void cron_scheduler_destroy(cron_scheduler_t *sched);

#endif
