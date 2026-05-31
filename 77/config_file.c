#include "config_file.h"
#include <ctype.h>

static char *trim(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static int parse_kv(const char *line, char *key, size_t klen,
                    char *val, size_t vlen) {
    const char *eq = strchr(line, '=');
    if (!eq) return -1;
    size_t kl = eq - line;
    if (kl >= klen) kl = klen - 1;
    strncpy(key, line, kl);
    key[kl] = '\0';
    char *kt = trim(key);
    if (kt != key) memmove(key, kt, strlen(kt) + 1);

    const char *vs = eq + 1;
    while (*vs == ' ' || *vs == '\t') vs++;
    strncpy(val, vs, vlen - 1);
    val[vlen - 1] = '\0';
    char *vt = trim(val);
    if (vt != val) memmove(val, vt, strlen(vt) + 1);

    size_t vl = strlen(val);
    while (vl > 0 && (val[vl - 1] == '\n' || val[vl - 1] == '\r' ||
                      val[vl - 1] == ' ' || val[vl - 1] == '\t')) {
        val[--vl] = '\0';
    }
    return 0;
}

int config_file_load(const char *path, app_config_t *config) {
    if (!path || !config) return -1;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "[config] 无法打开配置文件: %s\n", path);
        return -1;
    }

    char line[1024];
    char section[128] = "";
    rule_config_t *cur_rule = NULL;
    int rule_idx = -1;

    while (fgets(line, sizeof(line), fp)) {
        char *t = trim(line);
        if (*t == '#' || *t == ';' || *t == '\0') continue;

        if (*t == '[') {
            char *end = strchr(t, ']');
            if (end) {
                *end = '\0';
                strncpy(section, t + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';

                if (strncmp(section, "rule", 4) == 0) {
                    rule_idx++;
                    if (rule_idx >= MAX_RULES) {
                        fprintf(stderr, "[config] 规则数超出上限 %d\n",
                                MAX_RULES);
                        fclose(fp);
                        return -1;
                    }
                    cur_rule = &config->rules[rule_idx];
                    memset(cur_rule, 0, sizeof(*cur_rule));
                    strncpy(cur_rule->name, section,
                            sizeof(cur_rule->name) - 1);
                    cur_rule->burst_kbytes = 16;
                    cur_rule->latency_ms = 50;
                }
            }
            continue;
        }

        char key[256], val[512];
        if (parse_kv(t, key, sizeof(key), val, sizeof(val)) != 0) continue;

        if (strcmp(section, "global") == 0) {
            if (strcmp(key, "ifname") == 0) {
                strncpy(config->ifname, val, MAX_IFNAME - 1);
            } else if (strcmp(key, "ip") == 0) {
                strncpy(config->ip, val, sizeof(config->ip) - 1);
            } else if (strcmp(key, "netmask") == 0) {
                strncpy(config->netmask, val, sizeof(config->netmask) - 1);
            } else if (strcmp(key, "daemon") == 0) {
                config->daemon_mode = atoi(val);
            }
        } else if (cur_rule && strncmp(section, "rule", 4) == 0) {
            if (strcmp(key, "rate-limit") == 0) {
                strncpy(cur_rule->rate_limit, val,
                        sizeof(cur_rule->rate_limit) - 1);
            } else if (strcmp(key, "delay") == 0) {
                strncpy(cur_rule->delay, val,
                        sizeof(cur_rule->delay) - 1);
            } else if (strcmp(key, "loss") == 0) {
                strncpy(cur_rule->loss, val,
                        sizeof(cur_rule->loss) - 1);
            } else if (strcmp(key, "dup") == 0) {
                strncpy(cur_rule->dup, val,
                        sizeof(cur_rule->dup) - 1);
            } else if (strcmp(key, "reorder") == 0) {
                strncpy(cur_rule->reorder, val,
                        sizeof(cur_rule->reorder) - 1);
            } else if (strcmp(key, "burst") == 0) {
                cur_rule->burst_kbytes = atoi(val);
            } else if (strcmp(key, "latency") == 0) {
                cur_rule->latency_ms = atoi(val);
            }
        }
    }

    config->num_rules = rule_idx + 1;
    fclose(fp);

    printf("[config] 加载 %d 条规则\n", config->num_rules);
    return 0;
}

int config_file_validate(const app_config_t *config) {
    if (!config) return -1;
    if (config->ifname[0] == '\0') {
        fprintf(stderr, "[config] ifname 未配置\n");
        return -1;
    }
    if (config->num_rules <= 0) {
        fprintf(stderr, "[config] 未配置任何规则\n");
        return -1;
    }
    return 0;
}
