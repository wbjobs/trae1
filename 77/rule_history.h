#ifndef VSHAPER_RULE_HISTORY_H
#define VSHAPER_RULE_HISTORY_H

#include "common.h"

int  rule_history_init(rule_history_t *history);
int  rule_history_push(rule_history_t *history, const rule_config_t *old_rule,
                        const rule_config_t *new_rule, const char *reason,
                        const char *operator);
int  rule_history_rollback(rule_history_t *history, int steps,
                            rule_config_t *restored_rule);
void rule_history_print(const rule_history_t *history);
void rule_history_destroy(rule_history_t *history);

#endif
