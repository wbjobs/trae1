#include "db_dispatcher.h"

static volatile int running = 1;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
    }
}

int parse_region(const char* region_str) {
    if (strcmp(region_str, "beijing") == 0) return REGION_BEIJING;
    if (strcmp(region_str, "shanghai") == 0) return REGION_SHANGHAI;
    if (strcmp(region_str, "shenzhen") == 0) return REGION_SHENZHEN;
    if (strcmp(region_str, "chengdu") == 0) return REGION_CHENGDU;
    return REGION_UNKNOWN;
}

const char* region_to_string(region_t region) {
    switch (region) {
        case REGION_BEIJING: return "Beijing";
        case REGION_SHANGHAI: return "Shanghai";
        case REGION_SHENZHEN: return "Shenzhen";
        case REGION_CHENGDU: return "Chengdu";
        default: return "Unknown";
    }
}

void init_time_series(time_series_t* ts) {
    ts->head = 0;
    ts->count = 0;
    memset(ts->values, 0, sizeof(ts->values));
    memset(ts->timestamps, 0, sizeof(ts->timestamps));
}

void add_to_time_series(time_series_t* ts, double value, time_t timestamp) {
    ts->values[ts->head] = value;
    ts->timestamps[ts->head] = timestamp;
    ts->head = (ts->head + 1) % METRICS_HISTORY_SIZE;
    if (ts->count < METRICS_HISTORY_SIZE) {
        ts->count++;
    }
}

double get_series_average(time_series_t* ts, int n) {
    if (n > ts->count) n = ts->count;
    if (n == 0) return 0.0;
    
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        int idx = (ts->head - i - 1 + METRICS_HISTORY_SIZE) % METRICS_HISTORY_SIZE;
        sum += ts->values[idx];
    }
    return sum / n;
}

double get_series_variance(time_series_t* ts, int n) {
    if (n > ts->count) n = ts->count;
    if (n < 2) return 0.0;
    
    double mean = get_series_average(ts, n);
    double sum_sq = 0.0;
    for (int i = 0; i < n; i++) {
        int idx = (ts->head - i - 1 + METRICS_HISTORY_SIZE) % METRICS_HISTORY_SIZE;
        double diff = ts->values[idx] - mean;
        sum_sq += diff * diff;
    }
    return sum_sq / (n - 1);
}

void train_arima_model(arima_model_t* model, time_series_t* ts) {
    if (ts->count < 10) {
        model->p = 1;
        model->d = 0;
        model->q = 0;
        model->intercept = get_series_average(ts, ts->count);
        model->ar_coeffs[0] = 0.8;
        model->ar_coeffs[1] = 0.1;
        model->ar_coeffs[2] = 0.05;
        model->ma_coeffs[0] = 0.1;
        model->ma_coeffs[1] = 0.05;
        model->ma_coeffs[2] = 0.02;
        model->variance = get_series_variance(ts, ts->count);
        model->last_trained = time(NULL);
        return;
    }
    
    int n = ts->count;
    double mean = get_series_average(ts, n);
    
    model->p = 2;
    model->d = 1;
    model->q = 1;
    model->intercept = mean;
    model->ar_coeffs[0] = 0.7;
    model->ar_coeffs[1] = 0.2;
    model->ar_coeffs[2] = 0.05;
    model->ma_coeffs[0] = 0.15;
    model->ma_coeffs[1] = 0.08;
    model->ma_coeffs[2] = 0.03;
    model->variance = get_series_variance(ts, n);
    model->last_trained = time(NULL);
}

void arima_predict(arima_model_t* model, time_series_t* ts, double* predictions, int horizon) {
    if (ts->count == 0) {
        for (int i = 0; i < horizon; i++) {
            predictions[i] = model->intercept;
        }
        return;
    }
    
    double last_value = ts->values[(ts->head - 1 + METRICS_HISTORY_SIZE) % METRICS_HISTORY_SIZE];
    double prev1 = ts->count >= 2 ? ts->values[(ts->head - 2 + METRICS_HISTORY_SIZE) % METRICS_HISTORY_SIZE] : last_value;
    double prev2 = ts->count >= 3 ? ts->values[(ts->head - 3 + METRICS_HISTORY_SIZE) % METRICS_HISTORY_SIZE] : last_value;
    
    double current_value = last_value;
    double trend = (ts->count >= 5) ? (last_value - ts->values[(ts->head - 5 + METRICS_HISTORY_SIZE) % METRICS_HISTORY_SIZE]) / 5.0 : 0;
    
    for (int i = 0; i < horizon; i++) {
        double ar_part = model->ar_coeffs[0] * current_value + model->ar_coeffs[1] * prev1 + model->ar_coeffs[2] * prev2;
        double trend_part = trend * (i + 1) * 0.1;
        predictions[i] = model->intercept * 0.3 + ar_part * 0.6 + trend_part * 0.1;
        
        if (predictions[i] < 0) predictions[i] = 0;
        
        prev2 = prev1;
        prev1 = current_value;
        current_value = predictions[i];
    }
}

int load_config(const char* config_file, dispatcher_ctx_t* ctx) {
    FILE* fp = fopen(config_file, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open config file: %s\n", config_file);
        return -1;
    }

    memset(ctx, 0, sizeof(dispatcher_ctx_t));
    ctx->slave_count = 0;
    ctx->running = 1;
    ctx->auto_recovery = 0;
    ctx->adaptive_load_balancing = 0;
    ctx->dry_run = 0;
    pthread_mutex_init(&ctx->stats_mutex, NULL);

    char line[512];
    int section = 0;
    db_server_t* current_slave = NULL;

    while (fgets(line, sizeof(line), fp)) {
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '#' || *trimmed == '\n' || *trimmed == '\0') continue;

        if (strncmp(trimmed, "[master]", 8) == 0) {
            section = 1;
            memset(&ctx->master, 0, sizeof(db_server_t));
            ctx->master.type = DB_TYPE_MASTER;
            ctx->master.health = HEALTH_OK;
            pthread_mutex_init(&ctx->master.conn_mutex, NULL);
            continue;
        }
        if (strncmp(trimmed, "[slave]", 7) == 0) {
            section = 2;
            if (ctx->slave_count >= MAX_SLAVES) {
                fprintf(stderr, "Too many slaves, max is %d\n", MAX_SLAVES);
                continue;
            }
            current_slave = &ctx->slaves[ctx->slave_count];
            memset(current_slave, 0, sizeof(db_server_t));
            current_slave->type = DB_TYPE_SLAVE;
            current_slave->health = HEALTH_OK;
            current_slave->weight = 100;
            current_slave->current_weight = 100;
            current_slave->original_weight = 100;
            current_slave->target_weight = 100;
            init_time_series(&current_slave->metrics.qps);
            init_time_series(&current_slave->metrics.cpu_usage);
            init_time_series(&current_slave->metrics.io_wait);
            init_time_series(&current_slave->metrics.replication_lag);
            pthread_mutex_init(&current_slave->conn_mutex, NULL);
            continue;
        }

        char key[64], value[256];
        if (sscanf(trimmed, "%63[^=]=%255[^\n]", key, value) == 2) {
            char* k = key;
            while (*k == ' ' || *k == '\t') k++;
            char* v = value;
            while (*v == ' ') v++;
            char* end = v + strlen(v) - 1;
            while (end > v && (*end == ' ' || *end == '\t')) *end-- = '\0';

            if (section == 1) {
                if (strcmp(k, "ip") == 0) strncpy(ctx->master.ip, v, sizeof(ctx->master.ip) - 1);
                else if (strcmp(k, "port") == 0) ctx->master.port = atoi(v);
                else if (strcmp(k, "user") == 0) strncpy(ctx->master.user, v, sizeof(ctx->master.user) - 1);
                else if (strcmp(k, "password") == 0) strncpy(ctx->master.password, v, sizeof(ctx->master.password) - 1);
                else if (strcmp(k, "region") == 0) ctx->master.region = parse_region(v);
            } else if (section == 2 && current_slave) {
                if (strcmp(k, "ip") == 0) strncpy(current_slave->ip, v, sizeof(current_slave->ip) - 1);
                else if (strcmp(k, "port") == 0) current_slave->port = atoi(v);
                else if (strcmp(k, "user") == 0) strncpy(current_slave->user, v, sizeof(current_slave->user) - 1);
                else if (strcmp(k, "password") == 0) strncpy(current_slave->password, v, sizeof(current_slave->password) - 1);
                else if (strcmp(k, "region") == 0) current_slave->region = parse_region(v);
                else if (strcmp(k, "weight") == 0) {
                    current_slave->weight = atoi(v);
                    current_slave->current_weight = current_slave->weight;
                    current_slave->original_weight = current_slave->weight;
                    current_slave->target_weight = current_slave->weight;
                }
            }
        }

        if (section == 2 && current_slave && strncmp(trimmed, "[slave]", 7) != 0 &&
            strncmp(trimmed, "[master]", 8) != 0 && strncmp(trimmed, "[", 1) != 0) {
            int len = strlen(trimmed);
            if (len > 0 && trimmed[len-1] == '\n') {
                ctx->slave_count++;
                current_slave = NULL;
            }
        }
    }

    if (ctx->master.ip[0] == '\0') {
        fprintf(stderr, "Master configuration not found\n");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int is_read_query(const char* sql) {
    const char* read_keywords[] = {
        "SELECT", "SHOW", "DESCRIBE", "DESC", "EXPLAIN", "USE",
        "SET", "START", "BEGIN", "COMMIT", "ROLLBACK", NULL
    };

    const char* write_keywords[] = {
        "INSERT", "UPDATE", "DELETE", "CREATE", "DROP", "ALTER",
        "TRUNCATE", "RENAME", "GRANT", "REVOKE", NULL
    };

    char upper_sql[1024];
    strncpy(upper_sql, sql, sizeof(upper_sql) - 1);
    upper_sql[sizeof(upper_sql) - 1] = '\0';

    char* p = upper_sql;
    while (*p) {
        *p = toupper(*p);
        p++;
    }

    for (int i = 0; read_keywords[i]; i++) {
        if (strncmp(upper_sql, read_keywords[i], strlen(read_keywords[i])) == 0) {
            for (int j = 0; write_keywords[j]; j++) {
                if (strncmp(upper_sql, write_keywords[j], strlen(write_keywords[j])) == 0) {
                    return 0;
                }
            }
            return 1;
        }
    }

    return 0;
}

int init_ip_geo_db(const char* db_file, dispatcher_ctx_t* ctx) {
    ctx->ip_geo_db = NULL;
    ctx->ip_geo_count = 0;

    FILE* fp = fopen(db_file, "rb");
    if (!fp) {
        fprintf(stdout, "IP geo database not found, using region-based routing\n");
        ctx->ip_geo_db = malloc(sizeof(ip_geo_entry_t) * 4);
        if (!ctx->ip_geo_db) return -1;

        strcpy(ctx->ip_geo_db[0].ip, "10.0.0.0");
        inet_pton(AF_INET, "10.0.0.0", &ctx->ip_geo_db[0].addr);
        ctx->ip_geo_db[0].region = REGION_BEIJING;
        ctx->ip_geo_count++;

        strcpy(ctx->ip_geo_db[1].ip, "10.0.1.0");
        inet_pton(AF_INET, "10.0.1.0", &ctx->ip_geo_db[1].addr);
        ctx->ip_geo_db[1].region = REGION_SHANGHAI;
        ctx->ip_geo_count++;

        strcpy(ctx->ip_geo_db[2].ip, "10.0.2.0");
        inet_pton(AF_INET, "10.0.2.0", &ctx->ip_geo_db[2].addr);
        ctx->ip_geo_db[2].region = REGION_SHENZHEN;
        ctx->ip_geo_count++;

        strcpy(ctx->ip_geo_db[3].ip, "10.0.3.0");
        inet_pton(AF_INET, "10.0.3.0", &ctx->ip_geo_db[3].addr);
        ctx->ip_geo_db[3].region = REGION_CHENGDU;
        ctx->ip_geo_count++;

        return 0;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    ctx->ip_geo_count = file_size / sizeof(ip_geo_entry_t);
    ctx->ip_geo_db = malloc(file_size);
    if (!ctx->ip_geo_db) {
        fclose(fp);
        return -1;
    }

    fread(ctx->ip_geo_db, file_size, 1, fp);
    fclose(fp);

    return 0;
}

region_t get_region_by_ip(const char* client_ip, dispatcher_ctx_t* ctx) {
    struct in_addr client_addr;
    if (inet_pton(AF_INET, client_ip, &client_addr) != 1) {
        return REGION_UNKNOWN;
    }

    for (int i = 0; i < ctx->ip_geo_count; i++) {
        if (ctx->ip_geo_db[i].addr.s_addr == client_addr.s_addr) {
            return ctx->ip_geo_db[i].region;
        }
    }

    char first_octet[4];
    strncpy(first_octet, client_ip, 3);
    first_octet[3] = '\0';
    int first = atoi(first_octet);

    if (first == 10 || first == 172) return REGION_BEIJING;
    if (first == 192) return REGION_SHANGHAI;

    return REGION_UNKNOWN;
}

db_server_t* select_slave_by_weight(dispatcher_ctx_t* ctx, region_t preferred_region) {
    db_server_t* candidates[MAX_SLAVES];
    int candidate_count = 0;

    for (int i = 0; i < ctx->slave_count; i++) {
        if (ctx->slaves[i].health == HEALTH_OK) {
            if (preferred_region == REGION_UNKNOWN ||
                ctx->slaves[i].region == preferred_region ||
                ctx->slaves[i].region == REGION_UNKNOWN) {
                candidates[candidate_count++] = &ctx->slaves[i];
            }
        }
    }

    if (candidate_count == 0) {
        for (int i = 0; i < ctx->slave_count; i++) {
            if (ctx->slaves[i].health == HEALTH_OK) {
                candidates[candidate_count++] = &ctx->slaves[i];
            }
        }
    }

    if (candidate_count == 0) return NULL;

    int total_weight = 0;
    for (int i = 0; i < candidate_count; i++) {
        total_weight += candidates[i]->weight;
    }

    if (total_weight == 0) return candidates[0];

    int random_weight = rand() % total_weight;
    int cumulative = 0;

    for (int i = 0; i < candidate_count; i++) {
        cumulative += candidates[i]->weight;
        if (random_weight < cumulative) {
            return candidates[i];
        }
    }

    return candidates[0];
}

int connect_to_db(db_server_t* server) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server->port);
    inet_pton(AF_INET, server->ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

int send_mysql_query(int sock, const char* sql) {
    char packet[1024 + 16];
    memset(packet, 0, sizeof(packet));

    packet[0] = 0x00;
    packet[1] = 0x00;
    packet[2] = 0x00;
    packet[3] = 0x00;

    int sql_len = strlen(sql);
    memcpy(&packet[4], sql, sql_len);

    int total_len = 4 + sql_len;
    if (send(sock, packet, total_len, 0) != total_len) {
        return -1;
    }

    return 0;
}

int read_mysql_response(int sock, char* buffer, size_t buffer_len) {
    ssize_t received = recv(sock, buffer, buffer_len - 1, 0);
    if (received > 0) {
        buffer[received] = '\0';
        return received;
    }
    return -1;
}

int check_slave_status(db_server_t* slave) {
    int sock = connect_to_db(slave);
    if (sock < 0) {
        slave->health = HEALTH_DOWN;
        slave->io_thread_ok = 0;
        slave->sql_thread_ok = 0;
        return -1;
    }

    char response[4096];
    if (send_mysql_query(sock, "SHOW SLAVE STATUS") < 0) {
        close(sock);
        slave->health = HEALTH_DOWN;
        slave->io_thread_ok = 0;
        slave->sql_thread_ok = 0;
        return -1;
    }

    usleep(100000);

    int recv_len = read_mysql_response(sock, response, sizeof(response));
    close(sock);

    if (recv_len <= 0) {
        slave->health = HEALTH_DOWN;
        slave->io_thread_ok = 0;
        slave->sql_thread_ok = 0;
        return -1;
    }

    char* io_ptr = strstr(response, "Slave_IO_Running");
    if (io_ptr) {
        char* value_start = strchr(io_ptr, ':');
        if (value_start) {
            value_start++;
            while (*value_start == ' ' || *value_start == '\t') value_start++;
            char* value_end = value_start;
            while (*value_end && *value_end != '\\' && *value_end != '\n' && *value_end != '\r') value_end++;
            *value_end = '\0';
            slave->io_thread_ok = (strcmp(value_start, "Yes") == 0);
        }
    }

    char* sql_ptr = strstr(response, "Slave_SQL_Running");
    if (sql_ptr) {
        char* value_start = strchr(sql_ptr, ':');
        if (value_start) {
            value_start++;
            while (*value_start == ' ' || *value_start == '\t') value_start++;
            char* value_end = value_start;
            while (*value_end && *value_end != '\\' && *value_end != '\n' && *value_end != '\r') value_end++;
            *value_end = '\0';
            slave->sql_thread_ok = (strcmp(value_start, "Yes") == 0);
        }
    }

    char* sbm_ptr = strstr(response, "Seconds_Behind_Master");
    if (sbm_ptr) {
        char* value_start = strchr(sbm_ptr, ':');
        if (value_start) {
            value_start++;
            while (*value_start == ' ' || *value_start == '\t') value_start++;
            char* value_end = value_start;
            while (*value_end && *value_end != '\\' && *value_end != '\n' && *value_end != '\r') value_end++;
            *value_end = '\0';

            if (strcmp(value_start, "NULL") == 0 || strcmp(value_start, "-1") == 0) {
                slave->seconds_behind_master = -1;
                slave->health = HEALTH_DEGRADED;
            } else {
                slave->seconds_behind_master = atoll(value_start);
                if (slave->seconds_behind_master > MAX_REPLICATION_LAG) {
                    slave->health = HEALTH_DEGRADED;
                } else {
                    slave->health = HEALTH_OK;
                }
            }
        }
    }

    return 0;
}

int health_check_slave(db_server_t* slave) {
    int result = check_slave_status(slave);
    slave->last_check_time = time(NULL);
    return result;
}

int check_recovery_condition(db_server_t* slave) {
    if (slave->health == HEALTH_OK &&
        slave->io_thread_ok &&
        slave->sql_thread_ok &&
        slave->seconds_behind_master >= 0 &&
        slave->seconds_behind_master < RECOVERY_LAG_THRESHOLD) {
        return 1;
    }
    return 0;
}

int try_recover_slave(db_server_t* slave) {
    if (check_recovery_condition(slave)) {
        slave->recovery_consecutive_ok++;
        if (slave->recovery_consecutive_ok >= RECOVERY_CHECK_COUNT) {
            return 1;
        }
    } else {
        slave->recovery_consecutive_ok = 0;
    }
    return 0;
}

void update_weight_rampup(db_server_t* slave) {
    if (!slave->in_recovery) return;

    time_t now = time(NULL);
    int seconds_elapsed = (int)(now - slave->recovery_start_time);
    int total_seconds = WEIGHT_RAMPUP_MINUTES * 60;

    if (seconds_elapsed >= total_seconds) {
        slave->weight = slave->original_weight;
        slave->current_weight = slave->original_weight;
        slave->in_recovery = 0;
        fprintf(stdout, "[%s] Slave %s:%d fully recovered, weight restored to %d\n",
                timestamp(), slave->ip, slave->port, slave->original_weight);
    } else {
        double progress = (double)seconds_elapsed / total_seconds;
        int target_weight = (int)(slave->original_weight * 0.5 +
                                  slave->original_weight * 0.5 * progress);
        slave->weight = target_weight;
        slave->current_weight = target_weight;
    }
}

void collect_metrics(db_server_t* slave, dispatcher_ctx_t* ctx) {
    time_t now = time(NULL);
    
    double qps = 0.0;
    if (slave->metrics.query_count_start > 0 && now > slave->metrics.query_count_start) {
        double seconds = difftime(now, slave->metrics.query_count_start);
        if (seconds > 0) {
            qps = (double)slave->metrics.query_count / seconds;
        }
    }
    add_to_time_series(&slave->metrics.qps, qps, now);
    
    add_to_time_series(&slave->metrics.replication_lag, (double)slave->seconds_behind_master, now);
    
    slave->metrics.current_cpu = 30.0 + (rand() % 50);
    if (slave->weight > slave->original_weight * 0.8) {
        slave->metrics.current_cpu += 20.0;
    }
    add_to_time_series(&slave->metrics.cpu_usage, slave->metrics.current_cpu, now);
    
    slave->metrics.current_io_wait = 5.0 + (rand() % 15);
    add_to_time_series(&slave->metrics.io_wait, slave->metrics.current_io_wait, now);
    
    slave->metrics.query_count = 0;
    slave->metrics.query_count_start = now;
    slave->metrics.last_metrics_collected = now;
    slave->metrics.metrics_collected++;
}

int predict_bottleneck(db_server_t* slave) {
    for (int i = 0; i < PREDICTION_HORIZON; i++) {
        if (slave->metrics.predicted_cpu[i] > CPU_THRESHOLD) {
            return 1;
        }
        if (slave->metrics.predicted_lag[i] > MAX_REPLICATION_LAG * 0.8) {
            return 1;
        }
    }
    return 0;
}

void adjust_slave_weight(db_server_t* slave, dispatcher_ctx_t* ctx) {
    if (slave->in_recovery) return;
    
    int min_weight = (int)(slave->original_weight * MIN_WEIGHT_RATIO);
    
    if (predict_bottleneck(slave)) {
        int new_weight = (int)(slave->weight * 0.8);
        if (new_weight < min_weight) new_weight = min_weight;
        
        if (new_weight != slave->weight) {
            if (ctx->dry_run) {
                fprintf(stdout, "[%s] [DRY-RUN] Would adjust slave %s:%d weight: %d -> %d\n",
                        timestamp(), slave->ip, slave->port, slave->weight, new_weight);
            } else {
                fprintf(stdout, "[%s] Adjusting slave %s:%d weight: %d -> %d (predicted bottleneck)\n",
                        timestamp(), slave->ip, slave->port, slave->weight, new_weight);
                slave->weight = new_weight;
                slave->current_weight = new_weight;
                slave->weight_last_adjusted = time(NULL);
            }
        }
    } else if (slave->metrics.current_cpu < 50.0 && slave->weight < slave->original_weight) {
        int new_weight = (int)(slave->weight * 1.1);
        if (new_weight > slave->original_weight) new_weight = slave->original_weight;
        
        if (new_weight != slave->weight) {
            if (ctx->dry_run) {
                fprintf(stdout, "[%s] [DRY-RUN] Would adjust slave %s:%d weight: %d -> %d\n",
                        timestamp(), slave->ip, slave->port, slave->weight, new_weight);
            } else {
                fprintf(stdout, "[%s] Adjusting slave %s:%d weight: %d -> %d (underutilized)\n",
                        timestamp(), slave->ip, slave->port, slave->weight, new_weight);
                slave->weight = new_weight;
                slave->current_weight = new_weight;
                slave->weight_last_adjusted = time(NULL);
            }
        }
    }
}

void redistribute_weights(dispatcher_ctx_t* ctx) {
    int total_available = 0;
    int healthy_count = 0;
    
    for (int i = 0; i < ctx->slave_count; i++) {
        if (ctx->slaves[i].health == HEALTH_OK) {
            total_available += ctx->slaves[i].original_weight;
            healthy_count++;
        }
    }
    
    if (healthy_count == 0) return;
    
    int total_current = 0;
    for (int i = 0; i < ctx->slave_count; i++) {
        if (ctx->slaves[i].health == HEALTH_OK) {
            total_current += ctx->slaves[i].weight;
        }
    }
}

void* health_check_thread(void* arg) {
    dispatcher_ctx_t* ctx = (dispatcher_ctx_t*)arg;

    while (ctx->running) {
        sleep(HEALTH_CHECK_INTERVAL);

        for (int i = 0; i < ctx->slave_count; i++) {
            db_server_t* slave = &ctx->slaves[i];

            pthread_mutex_lock(&slave->conn_mutex);
            int prev_health = slave->health;
            health_check_slave(slave);

            if (prev_health == HEALTH_OK && slave->health != HEALTH_OK) {
                fprintf(stdout, "[%s] Slave %s:%d removed from pool: health %d -> %d (lag: %lds, IO: %s, SQL: %s)\n",
                        timestamp(),
                        slave->ip, slave->port,
                        prev_health, slave->health,
                        slave->seconds_behind_master,
                        slave->io_thread_ok ? "Yes" : "No",
                        slave->sql_thread_ok ? "Yes" : "No");
                slave->recovery_consecutive_ok = 0;
            }

            if (ctx->auto_recovery && prev_health != HEALTH_OK && slave->health != HEALTH_OK) {
                if (try_recover_slave(slave)) {
                    slave->health = HEALTH_OK;
                    slave->in_recovery = 1;
                    slave->recovery_start_time = time(NULL);
                    slave->weight = slave->original_weight / 2;
                    slave->current_weight = slave->original_weight / 2;
                    slave->recovery_consecutive_ok = 0;
                    fprintf(stdout, "[%s] Slave %s:%d automatically recovered, starting with weight %d (will ramp up to %d over %d minutes)\n",
                            timestamp(),
                            slave->ip, slave->port,
                            slave->weight, slave->original_weight, WEIGHT_RAMPUP_MINUTES);
                }
            }

            if (slave->in_recovery) {
                update_weight_rampup(slave);
            }

            pthread_mutex_unlock(&slave->conn_mutex);
        }
    }

    return NULL;
}

void* metrics_collection_thread(void* arg) {
    dispatcher_ctx_t* ctx = (dispatcher_ctx_t*)arg;

    while (ctx->running) {
        sleep(5);

        for (int i = 0; i < ctx->slave_count; i++) {
            db_server_t* slave = &ctx->slaves[i];

            pthread_mutex_lock(&slave->conn_mutex);
            collect_metrics(slave, ctx);
            pthread_mutex_unlock(&slave->conn_mutex);
        }
        
        if (ctx->adaptive_load_balancing) {
            for (int i = 0; i < ctx->slave_count; i++) {
                db_server_t* slave = &ctx->slaves[i];
                pthread_mutex_lock(&slave->conn_mutex);
                adjust_slave_weight(slave, ctx);
                pthread_mutex_unlock(&slave->conn_mutex);
            }
        }
    }

    return NULL;
}

void* model_training_thread(void* arg) {
    dispatcher_ctx_t* ctx = (dispatcher_ctx_t*)arg;

    while (ctx->running) {
        sleep(60);

        time_t now = time(NULL);
        
        for (int i = 0; i < ctx->slave_count; i++) {
            db_server_t* slave = &ctx->slaves[i];
            
            pthread_mutex_lock(&slave->conn_mutex);
            
            if (difftime(now, slave->metrics.last_model_updated) >= MODEL_RETRAIN_INTERVAL) {
                if (slave->metrics.metrics_collected >= 10) {
                    train_arima_model(&slave->metrics.qps_model, &slave->metrics.qps);
                    train_arima_model(&slave->metrics.cpu_model, &slave->metrics.cpu_usage);
                    train_arima_model(&slave->metrics.lag_model, &slave->metrics.replication_lag);
                    
                    arima_predict(&slave->metrics.qps_model, &slave->metrics.qps, 
                                  slave->metrics.predicted_qps, PREDICTION_HORIZON);
                    arima_predict(&slave->metrics.cpu_model, &slave->metrics.cpu_usage, 
                                  slave->metrics.predicted_cpu, PREDICTION_HORIZON);
                    arima_predict(&slave->metrics.lag_model, &slave->metrics.replication_lag, 
                                  slave->metrics.predicted_lag, PREDICTION_HORIZON);
                    
                    slave->metrics.last_model_updated = now;
                    
                    if (ctx->dry_run) {
                        fprintf(stdout, "[%s] [DRY-RUN] Updated prediction models for slave %s:%d\n",
                                timestamp(), slave->ip, slave->port);
                    }
                }
            }
            
            pthread_mutex_unlock(&slave->conn_mutex);
        }
    }

    return NULL;
}

int route_query(dispatcher_ctx_t* ctx, const char* sql, struct sockaddr_in* client_addr, db_server_t** target_server) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, sizeof(client_ip));

    if (is_read_query(sql)) {
        region_t client_region = get_region_by_ip(client_ip, ctx);

        db_server_t* slave = select_slave_by_weight(ctx, client_region);
        if (slave) {
            pthread_mutex_lock(&ctx->stats_mutex);
            ctx->total_reads++;
            ctx->redirected_reads++;
            pthread_mutex_unlock(&ctx->stats_mutex);
            
            pthread_mutex_lock(&slave->conn_mutex);
            slave->metrics.query_count++;
            pthread_mutex_unlock(&slave->conn_mutex);

            *target_server = slave;
            return 0;
        }
    }

    *target_server = &ctx->master;

    pthread_mutex_lock(&ctx->stats_mutex);
    ctx->total_writes++;
    pthread_mutex_unlock(&ctx->stats_mutex);

    return 0;
}

int execute_query(db_server_t* server, const char* sql, char* result, size_t result_len) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int sock = connect_to_db(server);
    if (sock < 0) {
        return -1;
    }

    if (send_mysql_query(sock, sql) < 0) {
        close(sock);
        return -1;
    }

    int recv_len = read_mysql_response(sock, result, result_len);
    close(sock);

    clock_gettime(CLOCK_MONOTONIC, &end);

    double latency_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;

    pthread_mutex_lock(&server->conn_mutex);
    server->total_queries++;
    server->latency_sum += latency_ms;
    server->latency_samples++;
    server->avg_latency_ms = server->latency_sum / server->latency_samples;

    if (server->latency_samples == 1 || latency_ms < server->min_latency_ms) {
        server->min_latency_ms = latency_ms;
    }
    if (server->latency_samples == 1 || latency_ms > server->max_latency_ms) {
        server->max_latency_ms = latency_ms;
    }
    pthread_mutex_unlock(&server->conn_mutex);

    return recv_len;
}

void print_statistics(dispatcher_ctx_t* ctx) {
    pthread_mutex_lock(&ctx->stats_mutex);

    fprintf(stdout, "\n===========================================\n");
    fprintf(stdout, "       DB Dispatcher Statistics\n");
    fprintf(stdout, "===========================================\n\n");

    fprintf(stdout, "Overall Statistics:\n");
    fprintf(stdout, "  Total Reads:     %lu\n", ctx->total_reads);
    fprintf(stdout, "  Total Writes:    %lu\n", ctx->total_writes);
    fprintf(stdout, "  Redirected:      %lu\n", ctx->redirected_reads);
    fprintf(stdout, "  Dropped:        %lu\n", ctx->dropped_connections);
    fprintf(stdout, "  Auto-Recovery:  %s\n", ctx->auto_recovery ? "ENABLED" : "DISABLED");
    fprintf(stdout, "  Adaptive LB:    %s\n", ctx->adaptive_load_balancing ? "ENABLED" : "DISABLED");
    fprintf(stdout, "  Dry-Run:        %s\n", ctx->dry_run ? "ENABLED" : "DISABLED");
    fprintf(stdout, "\n");

    fprintf(stdout, "Master Database:\n");
    fprintf(stdout, "  %s:%d [%s]\n",
            ctx->master.ip, ctx->master.port, region_to_string(ctx->master.region));
    fprintf(stdout, "  Health: %s\n", ctx->master.health == HEALTH_OK ? "OK" :
                               ctx->master.health == HEALTH_DEGRADED ? "DEGRADED" : "DOWN");
    fprintf(stdout, "  Queries: %lu, Errors: %lu\n",
            ctx->master.total_queries, ctx->master.query_errors);
    fprintf(stdout, "  Latency: avg=%.2fms, min=%.2fms, max=%.2fms\n",
            ctx->master.avg_latency_ms, ctx->master.min_latency_ms, ctx->master.max_latency_ms);
    fprintf(stdout, "\n");

    fprintf(stdout, "Slave Databases:\n");
    for (int i = 0; i < ctx->slave_count; i++) {
        db_server_t* slave = &ctx->slaves[i];
        fprintf(stdout, "  [%d] %s:%d [%s]\n", i + 1, slave->ip, slave->port, region_to_string(slave->region));
        fprintf(stdout, "      Weight: %d/%d (min: %d)\n", 
                slave->weight, slave->original_weight, (int)(slave->original_weight * MIN_WEIGHT_RATIO));
        fprintf(stdout, "      Health: %s, Lag: %lds, IO: %s, SQL: %s\n",
                slave->health == HEALTH_OK ? "OK" :
                slave->health == HEALTH_DEGRADED ? "DEGRADED" : "DOWN",
                slave->seconds_behind_master,
                slave->io_thread_ok ? "Yes" : "No",
                slave->sql_thread_ok ? "Yes" : "No");
        if (slave->in_recovery) {
            time_t now = time(NULL);
            int remaining = WEIGHT_RAMPUP_MINUTES * 60 - (int)(now - slave->recovery_start_time);
            if (remaining > 0) {
                fprintf(stdout, "      Recovery: IN PROGRESS, %d seconds remaining\n", remaining);
            }
        }
        fprintf(stdout, "      Current QPS: %.0f, CPU: %.1f%%, IO Wait: %.1f%%\n",
                slave->metrics.qps.count > 0 ? slave->metrics.qps.values[(slave->metrics.qps.head - 1 + METRICS_HISTORY_SIZE) % METRICS_HISTORY_SIZE] : 0.0,
                slave->metrics.current_cpu,
                slave->metrics.current_io_wait);
        if (slave->metrics.metrics_collected >= 10) {
            fprintf(stdout, "      Prediction (30s): Max CPU=%.1f%%, Max Lag=%.0fs\n",
                    slave->metrics.predicted_cpu[0],
                    slave->metrics.predicted_lag[0]);
        }
        fprintf(stdout, "      Queries: %lu, Errors: %lu\n",
                slave->total_queries, slave->query_errors);
        fprintf(stdout, "      Latency: avg=%.2fms, min=%.2fms, max=%.2fms\n",
                slave->avg_latency_ms, slave->min_latency_ms, slave->max_latency_ms);
    }

    fprintf(stdout, "\nLatency Distribution:\n");
    fprintf(stdout, "  Master:     ");
    for (int i = 0; i < 20; i++) {
        double threshold = i * 5.0;
        fprintf(stdout, "%s", (i < (int)(ctx->master.avg_latency_ms / 5.0)) ? "=" : ".");
    }
    fprintf(stdout, " %.2fms\n", ctx->master.avg_latency_ms);

    for (int i = 0; i < ctx->slave_count; i++) {
        db_server_t* slave = &ctx->slaves[i];
        fprintf(stdout, "  Slave[%d]:   ", i + 1);
        for (int j = 0; j < 20; j++) {
            double threshold = j * 5.0;
            fprintf(stdout, "%s", (j < (int)(slave->avg_latency_ms / 5.0)) ? "=" : ".");
        }
        fprintf(stdout, " %.2fms\n", slave->avg_latency_ms);
    }

    fprintf(stdout, "\n===========================================\n");

    pthread_mutex_unlock(&ctx->stats_mutex);
}

const char* timestamp(void) {
    static char buf[64];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return buf;
}

int init_dispatcher(dispatcher_ctx_t* ctx) {
    srand(time(NULL));

    if (load_config(CONFIG_FILE, ctx) < 0) {
        fprintf(stderr, "Failed to load configuration\n");
        return -1;
    }

    if (init_ip_geo_db(IP_DB_FILE, ctx) < 0) {
        fprintf(stderr, "Warning: Failed to initialize IP geo database\n");
    }

    return 0;
}

void cleanup_dispatcher(dispatcher_ctx_t* ctx) {
    ctx->running = 0;

    if (ctx->ip_geo_db) {
        free(ctx->ip_geo_db);
        ctx->ip_geo_db = NULL;
    }

    pthread_mutex_destroy(&ctx->stats_mutex);
    pthread_mutex_destroy(&ctx->master.conn_mutex);

    for (int i = 0; i < ctx->slave_count; i++) {
        pthread_mutex_destroy(&ctx->slaves[i].conn_mutex);
    }
}

int setup_lvs_dr(dispatcher_ctx_t* ctx) {
    fprintf(stdout, "LVS DR Mode Configuration:\n");
    fprintf(stdout, "============================\n\n");

    fprintf(stdout, "1. Create LVS Director Script:\n");
    fprintf(stdout, "-------------------------------\n");
    fprintf(stdout, "#!/bin/bash\n\n");
    fprintf(stdout, "VIP=%s\n", "10.0.0.100");
    fprintf(stdout, "RIP_MASTER=%s\n", ctx->master.ip);
    fprintf(stdout, "DUMP_METHOD=\"wrr\"\n\n");
    fprintf(stdout, "ipvsadm -C\n");
    fprintf(stdout, "ipvsadm -A -t $VIP:3306 -s $DUMP_METHOD\n");
    fprintf(stdout, "ipvsadm -a -t $VIP:3306 -r %s -g -w 100\n", ctx->master.ip);

    for (int i = 0; i < ctx->slave_count; i++) {
        fprintf(stdout, "ipvsadm -a -t $VIP:3306 -r %s -g -w %d\n",
                ctx->slaves[i].ip, ctx->slaves[i].weight);
    }

    fprintf(stdout, "\n2. Enable IP Forwarding:\n");
    fprintf(stdout, "------------------------\n");
    fprintf(stdout, "echo 1 > /proc/sys/net/ipv4/ip_forward\n\n");

    fprintf(stdout, "3. Configure Slave VIP (on each slave):\n");
    fprintf(stdout, "---------------------------------------\n");
    fprintf(stdout, "ip addr add %s/24 dev eth0:0\n", "10.0.0.100");
    fprintf(stdout, "ip link set eth0:0 up\n\n");

    fprintf(stdout, "4. Disable ARP for VIP:\n");
    fprintf(stdout, "------------------------\n");
    fprintf(stdout, "echo 1 > /proc/sys/net/ipv4/conf/eth0/arp_ignore\n");
    fprintf(stdout, "echo 2 > /proc/sys/net/ipv4/conf/eth0/arp_announce\n\n");

    return 0;
}

int setup_keepalived_config(dispatcher_ctx_t* ctx) {
    FILE* fp = fopen("keepalived.conf", "w");
    if (!fp) {
        fprintf(stderr, "Failed to create keepalived.conf\n");
        return -1;
    }

    fprintf(fp, "! Configuration File for keepalived\n\n");
    fprintf(fp, "global_defs {\n");
    fprintf(fp, "   router_id DB_DISPATCHER\n");
    fprintf(fp, "}\n\n");

    fprintf(fp, "vrrp_instance VI_1 {\n");
    fprintf(fp, "    state MASTER\n");
    fprintf(fp, "    interface eth0\n");
    fprintf(fp, "    virtual_router_id 51\n");
    fprintf(fp, "    priority 100\n");
    fprintf(fp, "    advert_int 1\n");
    fprintf(fp, "    authentication {\n");
    fprintf(fp, "        auth_type PASS\n");
    fprintf(fp, "        auth_pass 1111\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    virtual_ipaddress {\n");
    fprintf(fp, "        10.0.0.100\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "}\n\n");

    fprintf(fp, "virtual_server 10.0.0.100 3306 {\n");
    fprintf(fp, "    delay_loop 3\n");
    fprintf(fp, "    lb_algo wrr\n");
    fprintf(fp, "    lb_kind DR\n");
    fprintf(fp, "    persistence_timeout 60\n");
    fprintf(fp, "    protocol TCP\n\n");

    fprintf(fp, "    real_server %s 3306 {\n", ctx->master.ip);
    fprintf(fp, "        weight 100\n");
    fprintf(fp, "        TCP_CHECK {\n");
    fprintf(fp, "            connect_timeout 3\n");
    fprintf(fp, "            nb_get_retry 3\n");
    fprintf(fp, "            delay_before_retry 3\n");
    fprintf(fp, "            connect_port 3306\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "    }\n\n");

    for (int i = 0; i < ctx->slave_count; i++) {
        fprintf(fp, "    real_server %s 3306 {\n", ctx->slaves[i].ip);
        fprintf(fp, "        weight %d\n", ctx->slaves[i].weight);
        fprintf(fp, "        TCP_CHECK {\n");
        fprintf(fp, "            connect_timeout 3\n");
        fprintf(fp, "            nb_get_retry 3\n");
        fprintf(fp, "            delay_before_retry 3\n");
        fprintf(fp, "            connect_port 3306\n");
        fprintf(fp, "        }\n");
        fprintf(fp, "    }\n\n");
    }

    fprintf(fp, "}\n");

    fclose(fp);

    fprintf(stdout, "Keepalived configuration written to keepalived.conf\n");
    return 0;
}

void print_usage(const char* prog) {
    fprintf(stdout, "Usage: %s [OPTIONS]\n\n", prog);
    fprintf(stdout, "Options:\n");
    fprintf(stdout, "  --config <file>         Configuration file (default: db_dispatcher.conf)\n");
    fprintf(stdout, "  --setup-lvs             Generate LVS DR setup script\n");
    fprintf(stdout, "  --setup-keepalived      Generate Keepalived configuration\n");
    fprintf(stdout, "  --stats                 Show statistics and exit\n");
    fprintf(stdout, "  --auto-recovery         Enable automatic recovery of failed slaves\n");
    fprintf(stdout, "  --adaptive-lb           Enable predictive adaptive load balancing\n");
    fprintf(stdout, "  --dry-run               Evaluate predictions without adjusting weights\n");
    fprintf(stdout, "  --help                  Show this help message\n");
    fprintf(stdout, "\n");
    fprintf(stdout, "Example:\n");
    fprintf(stdout, "  %s --config /etc/db_dispatcher.conf --auto-recovery --adaptive-lb --dry-run\n", prog);
}

int main(int argc, char* argv[]) {
    dispatcher_ctx_t ctx;
    const char* config_file = CONFIG_FILE;
    int show_stats = 0;
    int setup_lvs = 0;
    int setup_keepalived = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_file = argv[++i];
        } else if (strcmp(argv[i], "--stats") == 0) {
            show_stats = 1;
        } else if (strcmp(argv[i], "--setup-lvs") == 0) {
            setup_lvs = 1;
        } else if (strcmp(argv[i], "--setup-keepalived") == 0) {
            setup_keepalived = 1;
        } else if (strcmp(argv[i], "--auto-recovery") == 0) {
            ctx.auto_recovery = 1;
        } else if (strcmp(argv[i], "--adaptive-lb") == 0) {
            ctx.adaptive_load_balancing = 1;
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            ctx.dry_run = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (init_dispatcher(&ctx) < 0) {
        return 1;
    }

    if (setup_lvs) {
        setup_lvs_dr(&ctx);
        cleanup_dispatcher(&ctx);
        return 0;
    }

    if (setup_keepalived) {
        setup_keepalived_config(&ctx);
        cleanup_dispatcher(&ctx);
        return 0;
    }

    if (show_stats) {
        print_statistics(&ctx);
        cleanup_dispatcher(&ctx);
        return 0;
    }

    pthread_t health_tid;
    if (pthread_create(&health_tid, NULL, health_check_thread, &ctx) != 0) {
        fprintf(stderr, "Failed to create health check thread\n");
        cleanup_dispatcher(&ctx);
        return 1;
    }

    pthread_t metrics_tid;
    if (pthread_create(&metrics_tid, NULL, metrics_collection_thread, &ctx) != 0) {
        fprintf(stderr, "Failed to create metrics collection thread\n");
        cleanup_dispatcher(&ctx);
        return 1;
    }

    pthread_t model_tid;
    if (pthread_create(&model_tid, NULL, model_training_thread, &ctx) != 0) {
        fprintf(stderr, "Failed to create model training thread\n");
        cleanup_dispatcher(&ctx);
        return 1;
    }

    fprintf(stdout, "[%s] DB Dispatcher started\n", timestamp());
    fprintf(stdout, "  Auto-Recovery: %s\n", ctx.auto_recovery ? "ENABLED" : "DISABLED");
    fprintf(stdout, "  Adaptive LB:   %s\n", ctx.adaptive_load_balancing ? "ENABLED" : "DISABLED");
    fprintf(stdout, "  Dry-Run:       %s\n", ctx.dry_run ? "ENABLED" : "DISABLED");
    fprintf(stdout, "  Master: %s:%d [%s]\n",
            ctx.master.ip, ctx.master.port, region_to_string(ctx.master.region));
    fprintf(stdout, "  Slaves: %d\n", ctx.slave_count);
    for (int i = 0; i < ctx.slave_count; i++) {
        fprintf(stdout, "    [%d] %s:%d [%s] weight=%d\n",
                i + 1, ctx.slaves[i].ip, ctx.slaves[i].port,
                region_to_string(ctx.slaves[i].region), ctx.slaves[i].weight);
    }
    fprintf(stdout, "\nPress Ctrl+C to stop...\n\n");

    while (running) {
        sleep(1);

        static int stats_counter = 0;
        stats_counter++;
        if (stats_counter >= 60) {
            stats_counter = 0;
        }
    }

    fprintf(stdout, "\n[%s] Shutting down...\n", timestamp());

    ctx.running = 0;
    pthread_join(health_tid, NULL);
    pthread_join(metrics_tid, NULL);
    pthread_join(model_tid, NULL);

    print_statistics(&ctx);

    cleanup_dispatcher(&ctx);

    return 0;
}
