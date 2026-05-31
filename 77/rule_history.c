#include "rule_history.h"
#include <time.h>

int rule_history_init(rule_history_t *history) {
    if (!history) return -1;
    memset(history, 0, sizeof(*history));
    pthread_mutex_init(&history->lock, NULL);
    history->count = 0;
    history->head = 0;
    return 0;
}

int rule_history_push(rule_history_t *history, const rule_config_t *old_rule,
                       const rule_config_t *new_rule, const char *reason,
                       const char *operator) {
    if (!history || !old_rule || !new_rule) return -1;

    pthread_mutex_lock(&history->lock);

    int idx = history->head;
    rule_history_entry_t *entry = &history->entries[idx];

    entry->timestamp = time(NULL);
    rule_config_copy(&entry->old_rule, old_rule);
    rule_config_copy(&entry->new_rule, new_rule);
    if (reason) strncpy(entry->reason, reason, sizeof(entry->reason) - 1);
    else entry->reason[0] = '\0';
    if (operator) strncpy(entry->operator, operator, sizeof(entry->operator) - 1);
    else strncpy(entry->operator, "system", sizeof(entry->operator) - 1);

    history->head = (history->head + 1) % MAX_HISTORY;
    if (history->count < MAX_HISTORY) history->count++;

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
             localtime(&entry->timestamp));
    printf("[history] 记录 #%d: %s → %s (%s by %s)\n",
           idx, old_rule->name, new_rule->name, reason, operator);
    printf("[history] 时间: %s\n", time_str);

    pthread_mutex_unlock(&history->lock);
    return 0;
}

int rule_history_rollback(rule_history_t *history, int steps,
                            rule_config_t *restored_rule) {
    if (!history || steps <= 0) return -1;

    pthread_mutex_lock(&history->lock);

    if (history->count == 0) {
        pthread_mutex_unlock(&history->lock);
        fprintf(stderr, "[history] 无可回滚的历史记录\n");
        return -1;
    }

    if (steps > history->count) steps = history->count;

    int idx = (history->head - steps + MAX_HISTORY) % MAX_HISTORY;
    if (idx < 0) idx += MAX_HISTORY;

    rule_history_entry_t *entry = &history->entries[idx];
    if (restored_rule) {
        rule_config_copy(restored_rule, &entry->old_rule);
    }

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
             localtime(&entry->timestamp));

    printf("[history] 回滚 %d 步: 恢复 '%s' (原记录: %s)\n",
           steps, entry->old_rule.name, time_str);

    pthread_mutex_unlock(&history->lock);
    return 0;
}

void rule_history_print(const rule_history_t *history) {
    if (!history) return;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║              规则历史记录 (%d 条)                        ║\n",
           history->count);
    printf("╠════╦════════════════════╦══════════╦══════════╦══════════╣\n");
    printf("║ #  ║ 时间               ║ 原规则   ║ 新规则   ║ 操作者   ║\n");
    printf("╠════╬════════════════════╬══════════╬══════════╬══════════╣\n");

    for (int i = 0; i < history->count; i++) {
        int idx = (history->head - 1 - i + MAX_HISTORY) % MAX_HISTORY;
        rule_history_entry_t *e = &history->entries[idx];

        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
                 localtime(&e->timestamp));

        printf("║ %-2d ║ %-18s ║ %-8s ║ %-8s ║ %-8s ║\n",
               i + 1, time_str, e->old_rule.name,
               e->new_rule.name, e->operator);
    }

    printf("╚════╩════════════════════╩══════════╩══════════╩══════════╝\n");
}

void rule_history_destroy(rule_history_t *history) {
    if (!history) return;
    pthread_mutex_destroy(&history->lock);
    memset(history, 0, sizeof(*history));
}
