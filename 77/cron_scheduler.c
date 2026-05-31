#include "cron_scheduler.h"
#include "tc_shaper.h"
#include "rule_history.h"
#include <ctype.h>

static int parse_field(const char *field, int *value, int *wild,
                        int *range_start, int *range_end, int *has_range,
                        int min_val, int max_val) {
    char buf[64];
    strncpy(buf, field, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *p = buf;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p == '*') {
        *wild = 1;
        *value = -1;
        *has_range = 0;
        return 0;
    }

    char *dash = strchr(p, '-');
    if (dash) {
        *dash = '\0';
        *range_start = atoi(p);
        *range_end = atoi(dash + 1);
        if (*range_start < min_val || *range_start > max_val ||
            *range_end < min_val || *range_end > max_val) {
            return -1;
        }
        *has_range = 1;
        *wild = 0;
        *value = -1;
        return 0;
    }

    *value = atoi(p);
    if (*value < min_val || *value > max_val) return -1;
    *wild = 0;
    *has_range = 0;
    return 0;
}

int cron_parse_expression(const char *expr, cron_expr_t *parsed) {
    if (!expr || !parsed) return -1;

    char buf[256];
    strncpy(buf, expr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *fields[5];
    int n = 0;
    char *p = strtok(buf, " \t");
    while (p && n < 5) {
        fields[n++] = p;
        p = strtok(NULL, " \t");
    }
    if (n != 5) {
        fprintf(stderr, "[cron] 表达式需要5个字段: %s\n", expr);
        return -1;
    }

    memset(parsed, 0, sizeof(*parsed));

    if (parse_field(fields[0], &parsed->minute, &parsed->minute_wild,
                     &parsed->minute_range_start, &parsed->minute_range_end,
                     &parsed->has_minute_range, 0, 59) != 0) {
        fprintf(stderr, "[cron] 解析分钟失败: %s\n", fields[0]);
        return -1;
    }

    if (parse_field(fields[1], &parsed->hour, &parsed->hour_wild,
                     &parsed->hour_range_start, &parsed->hour_range_end,
                     &parsed->has_hour_range, 0, 23) != 0) {
        fprintf(stderr, "[cron] 解析小时失败: %s\n", fields[1]);
        return -1;
    }

    if (parse_field(fields[2], &parsed->day, &parsed->day_wild,
                     NULL, NULL, NULL, 1, 31) != 0) {
        fprintf(stderr, "[cron] 解析日期失败: %s\n", fields[2]);
        return -1;
    }

    if (parse_field(fields[3], &parsed->month, &parsed->month_wild,
                     NULL, NULL, NULL, 1, 12) != 0) {
        fprintf(stderr, "[cron] 解析月份失败: %s\n", fields[3]);
        return -1;
    }

    if (parse_field(fields[4], &parsed->weekday, &parsed->weekday_wild,
                     NULL, NULL, NULL, 0, 6) != 0) {
        fprintf(stderr, "[cron] 解析星期失败: %s\n", fields[4]);
        return -1;
    }

    return 0;
}

static int in_range(int val, int start, int end) {
    if (start <= end) return (val >= start && val <= end);
    return (val >= start || val <= end);
}

int cron_expr_matches(const cron_expr_t *expr, const struct tm *tm) {
    if (!expr || !tm) return 0;

    if (expr->has_minute_range) {
        if (!in_range(tm->tm_min, expr->minute_range_start,
                       expr->minute_range_end)) return 0;
    } else if (!expr->minute_wild) {
        if (tm->tm_min != expr->minute) return 0;
    }

    if (expr->has_hour_range) {
        if (!in_range(tm->tm_hour, expr->hour_range_start,
                       expr->hour_range_end)) return 0;
    } else if (!expr->hour_wild) {
        if (tm->tm_hour != expr->hour) return 0;
    }

    if (!expr->day_wild) {
        if (tm->tm_mday != expr->day) return 0;
    }

    if (!expr->month_wild) {
        if (tm->tm_mon + 1 != expr->month) return 0;
    }

    if (!expr->weekday_wild) {
        int wday = tm->tm_wday;
        if (wday == 0) wday = 7;
        if (wday != expr->weekday) return 0;
    }

    return 1;
}

int cron_scheduler_init(cron_scheduler_t *sched) {
    if (!sched) return -1;
    memset(sched, 0, sizeof(*sched));
    pthread_mutex_init(&sched->lock, NULL);
    sched->running = 0;
    return 0;
}

int cron_scheduler_add_task(cron_scheduler_t *sched, const char *name,
                             const char *cron_expr, const rule_config_t *rule,
                             int priority) {
    if (!sched || !name || !cron_expr || !rule) return -1;
    if (sched->num_tasks >= MAX_CRON_ENTRIES) {
        fprintf(stderr, "[cron] 任务数超出上限 %d\n", MAX_CRON_ENTRIES);
        return -1;
    }

    pthread_mutex_lock(&sched->lock);

    cron_task_t *task = &sched->tasks[sched->num_tasks];
    memset(task, 0, sizeof(*task));
    strncpy(task->name, name, sizeof(task->name) - 1);
    strncpy(task->cron_expr, cron_expr, sizeof(task->cron_expr) - 1);
    rule_config_copy(&task->rule, rule);
    task->priority = priority;
    task->is_active = 0;
    task->last_triggered = 0;

    sched->num_tasks++;

    pthread_mutex_unlock(&sched->lock);

    printf("[cron] 已添加任务: %s (cron='%s')\n", name, cron_expr);
    return 0;
}

static tc_shaper_t g_sched_shaper;
static int g_sched_inited = 0;

static void apply_rule_smooth(const rule_config_t *new_rule,
                               const char *reason) {
    pthread_mutex_lock(&g_rule_lock);

    if (rule_config_equal(&g_current_rule, new_rule)) {
        pthread_mutex_unlock(&g_rule_lock);
        return;
    }

    printf("[cron] 规则切换: '%s' → '%s' (原因: %s)\n",
           g_current_rule.name, new_rule->name, reason);

    if (g_config.syslog_enabled) {
        syslog(LOG_INFO, "vshaper: rule switch '%s' -> '%s' (%s)",
               g_current_rule.name, new_rule->name, reason);
    }

    rule_history_push(&g_rule_history, &g_current_rule, new_rule,
                      reason, "scheduler");

    tc_shaper_remove(&g_sched_shaper);
    tc_shaper_init(&g_sched_shaper, g_config.ifname, new_rule);

    int transition_ms = g_config.transition_ms > 0
                        ? g_config.transition_ms : DEFAULT_TRANSITION_MS;
    printf("[cron] 平滑过渡: %dms\n", transition_ms);
    usleep((useconds_t)transition_ms * 1000);

    tc_shaper_apply(&g_sched_shaper);

    rule_config_copy(&g_current_rule, new_rule);

    pthread_mutex_unlock(&g_rule_lock);
}

static void *scheduler_thread(void *arg) {
    cron_scheduler_t *sched = (cron_scheduler_t *)arg;
    time_t last_minute = -1;

    printf("[cron] 调度线程已启动\n");

    while (sched->running && g_running) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        time_t current_minute = now / 60;

        if (current_minute != last_minute) {
            last_minute = current_minute;

            pthread_mutex_lock(&sched->lock);

            int best_idx = -1;
            int best_priority = -1;

            for (int i = 0; i < sched->num_tasks; i++) {
                cron_task_t *task = &sched->tasks[i];
                cron_expr_t parsed;
                if (cron_parse_expression(task->cron_expr, &parsed) == 0) {
                    if (cron_expr_matches(&parsed, tm_info)) {
                        if (task->priority > best_priority) {
                            best_priority = task->priority;
                            best_idx = i;
                        }
                    }
                }
            }

            pthread_mutex_unlock(&sched->lock);

            if (best_idx >= 0) {
                cron_task_t *task = &sched->tasks[best_idx];
                if (!task->is_active) {
                    apply_rule_smooth(&task->rule, task->name);
                    for (int i = 0; i < sched->num_tasks; i++) {
                        sched->tasks[i].is_active = (i == best_idx);
                    }
                }
            }
        }

        sleep(1);
    }

    printf("[cron] 调度线程已停止\n");
    return NULL;
}

void cron_scheduler_start(cron_scheduler_t *sched) {
    if (!sched || sched->running) return;

    if (!g_sched_inited) {
        memset(&g_sched_shaper, 0, sizeof(g_sched_shaper));
        g_sched_inited = 1;
    }

    sched->running = 1;
    pthread_create(&sched->thread, NULL, scheduler_thread, sched);
    printf("[cron] 调度器已启动 (%d 个任务)\n", sched->num_tasks);
}

void cron_scheduler_stop(cron_scheduler_t *sched) {
    if (!sched) return;
    sched->running = 0;
    pthread_join(sched->thread, NULL);
    if (g_sched_inited) {
        tc_shaper_destroy(&g_sched_shaper);
        g_sched_inited = 0;
    }
}

void cron_scheduler_destroy(cron_scheduler_t *sched) {
    if (!sched) return;
    cron_scheduler_stop(sched);
    pthread_mutex_destroy(&sched->lock);
    memset(sched, 0, sizeof(*sched));
}
