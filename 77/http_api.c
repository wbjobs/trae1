#include "http_api.h"
#include "tc_shaper.h"
#include "rule_history.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static tc_shaper_t g_http_shaper;
static int g_http_shaper_inited = 0;

static void send_response(int fd, const char *status, const char *body) {
    char header[MAX_HTTP_BUF];
    int body_len = body ? (int)strlen(body) : 0;

    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             status, body_len);

    send(fd, header, strlen(header), 0);
    if (body && body_len > 0) {
        send(fd, body, (size_t)body_len, 0);
    }
}

static void send_json_response(int fd, const char *status,
                                const char *json_body) {
    send_response(fd, status, json_body);
}

static char *extract_param(const char *body, const char *key) {
    static char value[256];
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"\\s*:\\s*\"([^\"]+)\"", key);

    regex_t regex;
    regmatch_t matches[2];

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) return NULL;
    if (regexec(&regex, body, 2, matches, 0) == 0) {
        int len = matches[1].rm_eo - matches[1].rm_so;
        if (len >= (int)sizeof(value)) len = (int)sizeof(value) - 1;
        strncpy(value, body + matches[1].rm_so, (size_t)len);
        value[len] = '\0';
        regfree(&regex);
        return value;
    }
    regfree(&regex);
    return NULL;
}

static char *extract_param_num(const char *body, const char *key) {
    static char value[256];
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"\\s*:\\s*([0-9.]+)", key);

    regex_t regex;
    regmatch_t matches[2];

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) return NULL;
    if (regexec(&regex, body, 2, matches, 0) == 0) {
        int len = matches[1].rm_eo - matches[1].rm_so;
        if (len >= (int)sizeof(value)) len = (int)sizeof(value) - 1;
        strncpy(value, body + matches[1].rm_so, (size_t)len);
        value[len] = '\0';
        regfree(&regex);
        return value;
    }
    regfree(&regex);
    return NULL;
}

static char *extract_param_int(const char *body, const char *key) {
    static char value[256];
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"\\s*:\\s*(\\d+)", key);

    regex_t regex;
    regmatch_t matches[2];

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) return NULL;
    if (regexec(&regex, body, 2, matches, 0) == 0) {
        int len = matches[1].rm_eo - matches[1].rm_so;
        if (len >= (int)sizeof(value)) len = (int)sizeof(value) - 1;
        strncpy(value, body + matches[1].rm_so, (size_t)len);
        value[len] = '\0';
        regfree(&regex);
        return value;
    }
    regfree(&regex);
    return NULL;
}

static void handle_get_status(int client_fd) {
    char body[MAX_HTTP_BUF];
    pthread_mutex_lock(&g_rule_lock);
    snprintf(body, sizeof(body),
             "{"
             "\"status\":\"running\","
             "\"interface\":\"%s\","
             "\"current_rule\":{"
             "\"name\":\"%s\","
             "\"rate_limit\":\"%s\","
             "\"delay\":\"%s\","
             "\"loss\":\"%s\","
             "\"dup\":\"%s\","
             "\"reorder\":\"%s\","
             "\"burst\":%d,"
             "\"latency\":%d"
             "}"
             "}",
             g_config.ifname,
             g_current_rule.name,
             g_current_rule.rate_limit,
             g_current_rule.delay,
             g_current_rule.loss,
             g_current_rule.dup,
             g_current_rule.reorder,
             g_current_rule.burst_kbytes,
             g_current_rule.latency_ms);
    pthread_mutex_unlock(&g_rule_lock);
    send_json_response(client_fd, "200 OK", body);
}

static void handle_get_stats(int client_fd) {
    stats_info_t stats;
    memset(&stats, 0, sizeof(stats));
    stats_collect(g_config.ifname, &stats);

    char body[MAX_HTTP_BUF];
    snprintf(body, sizeof(body),
             "{"
             "\"rx_packets\":%lu,"
             "\"tx_packets\":%lu,"
             "\"rx_bytes\":%lu,"
             "\"tx_bytes\":%lu,"
             "\"rx_dropped\":%lu,"
             "\"tx_dropped\":%lu,"
             "\"rx_errors\":%lu,"
             "\"tx_errors\":%lu,"
             "\"avg_delay_ms\":%.2f,"
             "\"lost_packets\":%lu,"
             "\"delayed_packets\":%lu,"
             "\"duplicated_packets\":%lu,"
             "\"reordered_packets\":%lu"
             "}",
             stats.rx_packets, stats.tx_packets,
             stats.rx_bytes, stats.tx_bytes,
             stats.rx_dropped, stats.tx_dropped,
             stats.rx_errors, stats.tx_errors,
             stats.avg_delay_ms,
             stats.lost_packets, stats.delayed_packets,
             stats.duplicated_packets, stats.reordered_packets);
    send_json_response(client_fd, "200 OK", body);
}

static void handle_post_update(int client_fd, const char *body) {
    char *new_rate = extract_param(body, "rate_limit");
    char *new_delay = extract_param(body, "delay");
    char *new_loss = extract_param(body, "loss");
    char *new_dup = extract_param(body, "dup");
    char *new_reorder = extract_param(body, "reorder");
    char *new_burst = extract_param_int(body, "burst");
    char *new_latency = extract_param_int(body, "latency");
    char *delay_add = extract_param(body, "delay_add");
    char *delay_sub = extract_param(body, "delay_sub");

    pthread_mutex_lock(&g_rule_lock);

    rule_config_t updated = g_current_rule;

    if (new_rate && new_rate[0]) strncpy(updated.rate_limit, new_rate,
                                          sizeof(updated.rate_limit) - 1);
    if (new_delay && new_delay[0]) strncpy(updated.delay, new_delay,
                                          sizeof(updated.delay) - 1);
    if (new_loss && new_loss[0]) strncpy(updated.loss, new_loss,
                                        sizeof(updated.loss) - 1);
    if (new_dup && new_dup[0]) strncpy(updated.dup, new_dup,
                                        sizeof(updated.dup) - 1);
    if (new_reorder && new_reorder[0]) strncpy(updated.reorder, new_reorder,
                                              sizeof(updated.reorder) - 1);
    if (new_burst && new_burst[0]) updated.burst_kbytes = atoi(new_burst);
    if (new_latency && new_latency[0]) updated.latency_ms = atoi(new_latency);

    if (delay_add && delay_add[0]) {
        unsigned long cur_us = 0, add_us = 0;
        parse_delay_value(updated.delay, &cur_us);
        parse_delay_value(delay_add, &add_us);
        unsigned long new_us = cur_us + add_us;
        snprintf(updated.delay, sizeof(updated.delay), "%lums", new_us / 1000);
    }
    if (delay_sub && delay_sub[0]) {
        unsigned long cur_us = 0, sub_us = 0;
        parse_delay_value(updated.delay, &cur_us);
        parse_delay_value(delay_sub, &sub_us);
        unsigned long new_us = (cur_us > sub_us) ? (cur_us - sub_us) : 0;
        snprintf(updated.delay, sizeof(updated.delay), "%lums", new_us / 1000);
    }

    if (rule_config_equal(&g_current_rule, &updated)) {
        pthread_mutex_unlock(&g_rule_lock);
        send_json_response(client_fd, "200 OK",
                           "{\"status\":\"no_change\"}");
        return;
    }

    rule_history_push(&g_rule_history, &g_current_rule, &updated,
                      "API update", "http_api");

    if (!g_http_shaper_inited) {
        memset(&g_http_shaper, 0, sizeof(g_http_shaper));
        g_http_shaper_inited = 1;
    }
    tc_shaper_remove(&g_http_shaper);
    tc_shaper_init(&g_http_shaper, g_config.ifname, &updated);

    int trans = g_config.transition_ms > 0 ? g_config.transition_ms : 5000;
    usleep((useconds_t)trans * 1000);

    tc_shaper_apply(&g_http_shaper);
    rule_config_copy(&g_current_rule, &updated);

    if (g_config.syslog_enabled) {
        syslog(LOG_INFO, "vshaper: API update rate=%s delay=%s loss=%s",
               updated.rate_limit, updated.delay, updated.loss);
    }

    pthread_mutex_unlock(&g_rule_lock);

    char resp[MAX_HTTP_BUF];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"rule\":{"
             "\"rate_limit\":\"%s\","
             "\"delay\":\"%s\","
             "\"loss\":\"%s\","
             "\"dup\":\"%s\","
             "\"reorder\":\"%s\""
             "}}",
             updated.rate_limit, updated.delay, updated.loss,
             updated.dup, updated.reorder);
    send_json_response(client_fd, "200 OK", resp);
}

static void handle_post_rollback(int client_fd, const char *body) {
    char *steps_str = extract_param_int(body, "steps");
    int steps = steps_str ? atoi(steps_str) : 1;

    rule_config_t restored;
    if (rule_history_rollback(&g_rule_history, steps, &restored) != 0) {
        send_json_response(client_fd, "400 Bad Request",
                           "{\"error\":\"no_history\"}");
        return;
    }

    pthread_mutex_lock(&g_rule_lock);

    rule_history_push(&g_rule_history, &g_current_rule, &restored,
                      "rollback", "http_api");

    if (!g_http_shaper_inited) {
        memset(&g_http_shaper, 0, sizeof(g_http_shaper));
        g_http_shaper_inited = 1;
    }
    tc_shaper_remove(&g_http_shaper);
    tc_shaper_init(&g_http_shaper, g_config.ifname, &restored);

    int trans = g_config.transition_ms > 0 ? g_config.transition_ms : 5000;
    usleep((useconds_t)trans * 1000);

    tc_shaper_apply(&g_http_shaper);
    rule_config_copy(&g_current_rule, &restored);

    pthread_mutex_unlock(&g_rule_lock);

    char resp[MAX_HTTP_BUF];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"rollback_ok\",\"restored_rule\":\"%s\"}",
             restored.name);
    send_json_response(client_fd, "200 OK", resp);
}

static void handle_request(int client_fd, const char *method,
                           const char *path, const char *body) {
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/api/status") == 0) {
            handle_get_status(client_fd);
        } else if (strcmp(path, "/api/stats") == 0) {
            handle_get_stats(client_fd);
        } else if (strcmp(path, "/api/history") == 0) {
            char hist_body[MAX_HTTP_BUF];
            snprintf(hist_body, sizeof(hist_body),
                     "{\"history_count\":%d}", g_rule_history.count);
            send_json_response(client_fd, "200 OK", hist_body);
        } else {
            send_json_response(client_fd, "404 Not Found",
                               "{\"error\":\"not_found\"}");
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/update") == 0) {
            handle_post_update(client_fd, body);
        } else if (strcmp(path, "/api/rollback") == 0) {
            handle_post_rollback(client_fd, body);
        } else {
            send_json_response(client_fd, "404 Not Found",
                               "{\"error\":\"not_found\"}");
        }
    } else {
        send_json_response(client_fd, "405 Method Not Allowed",
                           "{\"error\":\"method_not_allowed\"}");
    }
}

static void *http_server_thread(void *arg) {
    http_api_server_t *server = (http_api_server_t *)arg;

    printf("[http] API 服务已启动: http://0.0.0.0:%d\n", server->port);
    printf("[http] 可用端点:\n");
    printf("[http]   GET  /api/status    - 当前规则状态\n");
    printf("[http]   GET  /api/stats     - 统计信息\n");
    printf("[http]   POST /api/update    - 更新规则参数\n");
    printf("[http]   POST /api/rollback  - 回滚规则\n");

    while (server->running && g_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server->server_fd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select(server->server_fd + 1, &read_fds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        int client_fd = accept(server->server_fd,
                               (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) continue;

        char buf[MAX_HTTP_BUF];
        memset(buf, 0, sizeof(buf));
        ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            close(client_fd);
            continue;
        }

        char method[16] = "", path[256] = "", body[MAX_HTTP_BUF] = "";
        sscanf(buf, "%15s %255s", method, path);

        char *body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            strncpy(body, body_start + 4, sizeof(body) - 1);
        }

        printf("[http] %s %s from %s\n", method, path,
               inet_ntoa(client_addr.sin_addr));

        handle_request(client_fd, method, path, body);
        close(client_fd);
    }

    printf("[http] API 服务已停止\n");
    return NULL;
}

int http_api_init(http_api_server_t *server, int port) {
    if (!server) return -1;
    memset(server, 0, sizeof(*server));

    server->port = port > 0 ? port : DEFAULT_HTTP_PORT;
    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        fprintf(stderr, "[http] socket 创建失败: %s\n", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)server->port);

    if (bind(server->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[http] bind 失败 (port %d): %s\n",
                server->port, strerror(errno));
        close(server->server_fd);
        server->server_fd = -1;
        return -1;
    }

    if (listen(server->server_fd, MAX_HTTP_CLIENTS) < 0) {
        fprintf(stderr, "[http] listen 失败: %s\n", strerror(errno));
        close(server->server_fd);
        server->server_fd = -1;
        return -1;
    }

    server->running = 0;
    pthread_mutex_init(&server->lock, NULL);
    return 0;
}

void http_api_start(http_api_server_t *server) {
    if (!server || server->running) return;
    server->running = 1;
    pthread_create(&server->thread, NULL, http_server_thread, server);
}

void http_api_stop(http_api_server_t *server) {
    if (!server) return;
    server->running = 0;
    pthread_join(server->thread, NULL);
}

void http_api_destroy(http_api_server_t *server) {
    if (!server) return;
    http_api_stop(server);
    if (server->server_fd >= 0) close(server->server_fd);
    pthread_mutex_destroy(&server->lock);
    if (g_http_shaper_inited) {
        tc_shaper_destroy(&g_http_shaper);
        g_http_shaper_inited = 0;
    }
    memset(server, 0, sizeof(*server));
}
