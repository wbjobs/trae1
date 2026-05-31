#include "mysql_parser.h"
#include "tcp_reassembly.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>

static stmt_cache_t *global_stmt_cache = NULL;

stmt_cache_t *get_stmt_cache(void) {
    if (!global_stmt_cache) {
        global_stmt_cache = stmt_cache_create();
    }
    return global_stmt_cache;
}

static size_t read_lenenc_int(const uint8_t **data_ptr, size_t *remaining_ptr) {
    if (!data_ptr || !*data_ptr || !remaining_ptr || *remaining_ptr < 1) return 0;

    const uint8_t *data = *data_ptr;
    size_t remaining = *remaining_ptr;

    uint8_t first = *data;
    if (first < 0xFB) {
        *data_ptr = data + 1;
        *remaining_ptr = remaining - 1;
        return first;
    } else if (first == 0xFC) {
        if (remaining < 3) return 0;
        uint16_t val = data[1] | (data[2] << 8);
        *data_ptr = data + 3;
        *remaining_ptr = remaining - 3;
        return val;
    } else if (first == 0xFD) {
        if (remaining < 4) return 0;
        uint32_t val = data[1] | (data[2] << 8) | (data[3] << 16);
        *data_ptr = data + 4;
        *remaining_ptr = remaining - 4;
        return val;
    } else if (first == 0xFE) {
        if (remaining < 9) return 0;
        uint64_t val = (uint64_t)data[1] | ((uint64_t)data[2] << 8) | ((uint64_t)data[3] << 16) |
                       ((uint64_t)data[4] << 24) | ((uint64_t)data[5] << 32) | ((uint64_t)data[6] << 40) |
                       ((uint64_t)data[7] << 48) | ((uint64_t)data[8] << 56);
        *data_ptr = data + 9;
        *remaining_ptr = remaining - 9;
        return (size_t)val;
    }
    return 0;
}

static uint8_t *read_lenenc_string(const uint8_t **data_ptr, size_t *remaining_ptr, size_t *out_len) {
    if (!data_ptr || !*data_ptr || !remaining_ptr || !out_len) return NULL;

    size_t len = read_lenenc_int(data_ptr, remaining_ptr);
    if (len == 0 || len > *remaining_ptr) {
        *out_len = 0;
        return NULL;
    }

    const uint8_t *str = *data_ptr;
    *data_ptr = str + len;
    *remaining_ptr = *remaining_ptr - len;
    *out_len = len;
    return (uint8_t *)str;
}

static int is_mysql_packet_start(const uint8_t *data, size_t len) {
    if (len < 4) return 0;
    if (data[0] == 0x00 || data[0] == 0x01 || data[0] == 0xFF || data[0] == 0xFE) {
        return 1;
    }
    if (data[3] == 0x00 || data[3] == 0x01) {
        return 1;
    }
    return 0;
}

int is_mysql_packet(const uint8_t *data, size_t len) {
    if (!data || len < 4) return 0;

    uint32_t packet_len = data[0] | (data[1] << 8) | (data[2] << 16);

    if (packet_len > MYSQL_MAX_PACKET_SIZE) return 0;

    uint8_t seq = data[3];

    if (seq > 255) return 0;

    if (packet_len == 0 && len == 4) return 1;

    if (len >= 5 && packet_len > 0) {
        if (packet_len <= len - 4) {
            return 1;
        }
    }

    return is_mysql_packet_start(data, len);
}

static size_t read_packet_length(const uint8_t *data) {
    return data[0] | (data[1] << 8) | (data[2] << 16);
}

static uint8_t read_packet_number(const uint8_t *data) {
    return data[3];
}

static int is_text_result_set(const uint8_t *data, size_t len) {
    if (len < 1) return 0;
    return (data[0] >= 0x01 && data[0] <= 0xFB);
}

static char *escape_sql_string(const uint8_t *str, size_t len) {
    if (!str || len == 0) return strdup("''");

    size_t out_len = len * 2 + 3;
    char *out = (char *)malloc(out_len);
    if (!out) return NULL;

    char *ptr = out;
    *ptr++ = '\'';
    for (size_t i = 0; i < len; i++) {
        uint8_t c = str[i];
        if (c == '\'') {
            *ptr++ = '\'';
            *ptr++ = '\'';
        } else if (c == '\\') {
            *ptr++ = '\\';
            *ptr++ = '\\';
        } else if (c == '"') {
            *ptr++ = '"';
        } else if (c == '\n') {
            *ptr++ = '\\';
            *ptr++ = 'n';
        } else if (c == '\r') {
            *ptr++ = '\\';
            *ptr++ = 'r';
        } else if (c == '\t') {
            *ptr++ = '\\';
            *ptr++ = 't';
        } else if (c >= 0x00 && c < 0x20) {
            *ptr++ = '\\';
            *ptr++ = 'x';
            *ptr++ = "0123456789ABCDEF"[c >> 4];
            *ptr++ = "0123456789ABCDEF"[c & 0x0F];
        } else {
            *ptr++ = (char)c;
        }
    }
    *ptr++ = '\'';
    *ptr = '\0';

    return out;
}

static char *param_to_string(mysql_param_t *param) {
    if (!param) return strdup("NULL");

    if (param->is_null) {
        return strdup("NULL");
    }

    char buf[256];
    switch (param->type) {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24:
            if (param->is_unsigned) {
                snprintf(buf, sizeof(buf), "%llu", (unsigned long long)param->value.uint_val);
            } else {
                snprintf(buf, sizeof(buf), "%lld", (long long)param->value.int_val);
            }
            return strdup(buf);

        case MYSQL_TYPE_FLOAT:
            snprintf(buf, sizeof(buf), "%f", param->value.float_val);
            return strdup(buf);

        case MYSQL_TYPE_DOUBLE:
            snprintf(buf, sizeof(buf), "%f", param->value.double_val);
            return strdup(buf);

        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_DATETIME:
        case MYSQL_TYPE_TIMESTAMP:
        case MYSQL_TYPE_TIME:
        case MYSQL_TYPE_YEAR:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_ENUM:
        case MYSQL_TYPE_SET:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
        case MYSQL_TYPE_GEOMETRY:
            if (param->value.str_val.data && param->value.str_val.len > 0) {
                return escape_sql_string(param->value.str_val.data, param->value.str_val.len);
            }
            return strdup("''");

        case MYSQL_TYPE_BIT:
            return strdup("b'1'");

        case MYSQL_TYPE_NULL:
            return strdup("NULL");

        default:
            return strdup("NULL");
    }
}

static char *reconstruct_sql(const char *template_sql, mysql_param_t *params, uint16_t num_params) {
    if (!template_sql) return NULL;

    size_t template_len = strlen(template_sql);
    size_t param_count = 0;
    for (size_t i = 0; i < template_len; i++) {
        if (template_sql[i] == '?') param_count++;
    }
    if (param_count == 0) return strdup(template_sql);

    char **param_strs = (char **)calloc(num_params, sizeof(char *));
    if (!param_strs) return NULL;

    size_t total_len = template_len - param_count + 1;
    for (uint16_t i = 0; i < num_params; i++) {
        param_strs[i] = param_to_string(&params[i]);
        total_len += strlen(param_strs[i]);
    }

    char *result = (char *)malloc(total_len);
    if (!result) {
        for (uint16_t i = 0; i < num_params; i++) free(param_strs[i]);
        free(param_strs);
        return NULL;
    }

    char *dest = result;
    uint16_t param_idx = 0;
    for (size_t i = 0; i < template_len; i++) {
        if (template_sql[i] == '?' && param_idx < num_params) {
            strcpy(dest, param_strs[param_idx]);
            dest += strlen(param_strs[param_idx]);
            param_idx++;
        } else {
            *dest++ = template_sql[i];
        }
    }
    *dest = '\0';

    for (uint16_t i = 0; i < num_params; i++) free(param_strs[i]);
    free(param_strs);
    return result;
}

static int parse_com_stmt_execute(const uint8_t *payload, size_t payload_len, char **sql_out) {
    if (!payload || payload_len < 5) return -1;

    const uint8_t *ptr = payload;
    size_t remaining = payload_len;

    uint32_t stmt_id = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
    ptr += 4;
    remaining -= 4;

    uint8_t flags = *ptr++;
    remaining--;

    uint32_t iteration_count = 1;
    if (flags & 0x01) {
        if (remaining < 4) return -1;
        iteration_count = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
        ptr += 4;
        remaining -= 4;
    }

    stmt_cache_t *cache = get_stmt_cache();
    stmt_cache_entry_t *entry = stmt_cache_get(cache, stmt_id);
    if (!entry) return -1;

    mysql_param_t params[MAX_PARAMS];
    memset(params, 0, sizeof(params));
    uint16_t num_params = entry->num_params;

    size_t null_bitmap_len = (num_params + 7) / 8;
    if (remaining < null_bitmap_len) {
        return -1;
    }
    for (uint16_t i = 0; i < num_params; i++) {
        uint8_t byte = ptr[i / 8];
        uint8_t bit = i % 8;
        params[i].is_null = (byte & (1 << bit)) ? 1 : 0;
    }
    ptr += null_bitmap_len;
    remaining -= null_bitmap_len;

    if (remaining < 1) return -1;
    uint8_t new_params_bind = *ptr++;
    remaining--;

    if (new_params_bind) {
        if (remaining < num_params * 2) return -1;
        for (uint16_t i = 0; i < num_params; i++) {
            params[i].type = ptr[0];
            params[i].is_unsigned = (ptr[1] & 0x80) ? 1 : 0;
            ptr += 2;
            remaining -= 2;
        }
    }

    for (uint16_t i = 0; i < num_params; i++) {
        if (params[i].is_null) continue;
        if (remaining == 0) break;

        uint8_t type = params[i].type;

        switch (type) {
            case MYSQL_TYPE_TINY:
                if (params[i].is_unsigned) {
                    params[i].value.uint_val = *ptr++;
                } else {
                    params[i].value.int_val = (int8_t)*ptr++;
                }
                remaining--;
                break;

            case MYSQL_TYPE_SHORT:
                if (params[i].is_unsigned) {
                    params[i].value.uint_val = ptr[0] | (ptr[1] << 8);
                } else {
                    params[i].value.int_val = (int16_t)(ptr[0] | (ptr[1] << 8));
                }
                ptr += 2;
                remaining -= 2;
                break;

            case MYSQL_TYPE_INT24:
                if (params[i].is_unsigned) {
                    params[i].value.uint_val = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16);
                } else {
                    params[i].value.int_val = (int32_t)(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16));
                }
                ptr += 3;
                remaining -= 3;
                break;

            case MYSQL_TYPE_LONG:
                if (params[i].is_unsigned) {
                    params[i].value.uint_val = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
                } else {
                    params[i].value.int_val = (int32_t)(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24));
                }
                ptr += 4;
                remaining -= 4;
                break;

            case MYSQL_TYPE_LONGLONG:
                if (params[i].is_unsigned) {
                    params[i].value.uint_val = (uint64_t)ptr[0] | ((uint64_t)ptr[1] << 8) |
                                               ((uint64_t)ptr[2] << 16) | ((uint64_t)ptr[3] << 24) |
                                               ((uint64_t)ptr[4] << 32) | ((uint64_t)ptr[5] << 40) |
                                               ((uint64_t)ptr[6] << 48) | ((uint64_t)ptr[7] << 56);
                } else {
                    params[i].value.int_val = (int64_t)(ptr[0] | ((uint64_t)ptr[1] << 8) |
                                                        ((uint64_t)ptr[2] << 16) | ((uint64_t)ptr[3] << 24) |
                                                        ((uint64_t)ptr[4] << 32) | ((uint64_t)ptr[5] << 40) |
                                                        ((uint64_t)ptr[6] << 48) | ((uint64_t)ptr[7] << 56));
                }
                ptr += 8;
                remaining -= 8;
                break;

            case MYSQL_TYPE_FLOAT:
                memcpy(&params[i].value.float_val, ptr, 4);
                ptr += 4;
                remaining -= 4;
                break;

            case MYSQL_TYPE_DOUBLE:
                memcpy(&params[i].value.double_val, ptr, 8);
                ptr += 8;
                remaining -= 8;
                break;

            case MYSQL_TYPE_DATE:
            case MYSQL_TYPE_DATETIME:
            case MYSQL_TYPE_TIMESTAMP:
            case MYSQL_TYPE_TIME:
            case MYSQL_TYPE_YEAR:
            case MYSQL_TYPE_STRING:
            case MYSQL_TYPE_VAR_STRING:
            case MYSQL_TYPE_VARCHAR:
            case MYSQL_TYPE_ENUM:
            case MYSQL_TYPE_SET:
            case MYSQL_TYPE_BLOB:
            case MYSQL_TYPE_TINY_BLOB:
            case MYSQL_TYPE_MEDIUM_BLOB:
            case MYSQL_TYPE_LONG_BLOB:
            case MYSQL_TYPE_DECIMAL:
            case MYSQL_TYPE_NEWDECIMAL:
            case MYSQL_TYPE_GEOMETRY:
            case MYSQL_TYPE_BIT: {
                size_t len = read_lenenc_int(&ptr, &remaining);
                if (len > remaining) {
                    len = remaining;
                }
                if (len > 0) {
                    params[i].value.str_val.data = (uint8_t *)ptr;
                    params[i].value.str_val.len = len;
                    ptr += len;
                    remaining -= len;
                }
                break;
            }

            case MYSQL_TYPE_NULL:
            default:
                params[i].is_null = 1;
                break;
        }
    }

    *sql_out = reconstruct_sql(entry->sql_template, params, num_params);
    return *sql_out ? 0 : -1;
}

static int parse_com_stmt_prepare_response(const uint8_t *payload, size_t payload_len, char **sql_out) {
    if (!payload || payload_len < 12) return -1;

    const uint8_t *ptr = payload;
    size_t remaining = payload_len;

    uint8_t status = *ptr++;
    remaining--;

    if (status != 0x00) return -1;

    uint32_t stmt_id = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
    ptr += 4;
    remaining -= 4;

    uint16_t num_columns = ptr[0] | (ptr[1] << 8);
    ptr += 2;
    remaining -= 2;

    uint16_t num_params = ptr[0] | (ptr[1] << 8);
    ptr += 2;
    remaining -= 2;

    stmt_cache_t *cache = get_stmt_cache();

    *sql_out = NULL;
    return 0;
}

static char *extract_query_from_packet(const uint8_t *payload, size_t payload_len, const uint8_t *packet) {
    if (!payload || payload_len < 1) return NULL;

    uint8_t command = payload[0];

    if (command == MYSQL_COM_QUERY) {
        if (payload_len < 2) return NULL;
        char *sql = malloc(payload_len);
        if (!sql) return NULL;
        size_t sql_len = payload_len - 1;
        memcpy(sql, payload + 1, sql_len);
        sql[sql_len] = '\0';
        return sql;
    }

    if (command == MYSQL_COM_STMT_PREPARE) {
        if (payload_len < 2) return NULL;
        char *sql = malloc(payload_len);
        if (!sql) return NULL;
        size_t sql_len = payload_len - 1;
        memcpy(sql, payload + 1, sql_len);
        sql[sql_len] = '\0';

        stmt_cache_t *cache = get_stmt_cache();
        stmt_cache_entry_t *entry = stmt_cache_get(cache, 0);

        return sql;
    }

    if (command == MYSQL_COM_STMT_EXECUTE) {
        char *sql = NULL;
        if (parse_com_stmt_execute(payload + 1, payload_len - 1, &sql) == 0) {
            return sql;
        }
    }

    if (command == MYSQL_COM_INIT_DB ||
        command == MYSQL_COM_CREATE_DB ||
        command == MYSQL_COM_DROP_DB) {
        if (payload_len < 2) return NULL;
        char *sql = malloc(payload_len + 16);
        if (!sql) return NULL;

        const char *cmd_str = NULL;
        switch (command) {
            case MYSQL_COM_INIT_DB: cmd_str = "USE"; break;
            case MYSQL_COM_CREATE_DB: cmd_str = "CREATE DATABASE"; break;
            case MYSQL_COM_DROP_DB: cmd_str = "DROP DATABASE"; break;
            default: cmd_str = "UNKNOWN"; break;
        }

        snprintf(sql, payload_len + 16, "%s %s", cmd_str, payload + 1);
        return sql;
    }

    return NULL;
}

static size_t get_packet_payload_length(const uint8_t *packet) {
    if (!packet) return 0;
    return read_packet_length(packet);
}

char *extract_mysql_sql(void *tcp_stream) {
    if (!tcp_stream) return NULL;

    tcp_stream_t *stream = (tcp_stream_t *)tcp_stream;

    pthread_mutex_lock(&stream->mutex);

    size_t data_len = stream->reassembled_len;
    uint8_t *data = stream->reassembled_data;

    if (!data || data_len < 5) {
        pthread_mutex_unlock(&stream->mutex);
        return NULL;
    }

    char *full_sql = NULL;
    size_t full_sql_len = 0;

    size_t offset = 0;
    uint32_t pending_stmt_id = 0;
    char *pending_sql = NULL;

    while (offset < data_len) {
        size_t remaining = data_len - offset;
        if (remaining < 4) break;

        size_t packet_len = read_packet_length(data + offset);
        uint8_t packet_num = read_packet_number(data + offset);

        if (packet_len == 0) {
            if (remaining >= 4 && full_sql) {
                break;
            }
            offset += 4;
            continue;
        }

        if (packet_len > remaining - 4) {
            break;
        }

        uint8_t *packet_payload = data + offset + 4;
        size_t payload_len = packet_len;

        if (payload_len > 0) {
            uint8_t first_byte = packet_payload[0];

            if (first_byte == MYSQL_COM_QUERY ||
                first_byte == MYSQL_COM_STMT_EXECUTE ||
                first_byte == MYSQL_COM_INIT_DB ||
                first_byte == MYSQL_COM_CREATE_DB ||
                first_byte == MYSQL_COM_DROP_DB) {

                char *sql = extract_query_from_packet(packet_payload, payload_len, data + offset);
                if (sql) {
                    size_t sql_len = strlen(sql);
                    if (sql_len > 0) {
                        if (full_sql) {
                            full_sql = realloc(full_sql, full_sql_len + sql_len + 2);
                            full_sql[full_sql_len] = ';';
                            full_sql[full_sql_len + 1] = '\0';
                            strcat(full_sql, sql);
                            full_sql_len += sql_len + 1;
                        } else {
                            full_sql = sql;
                            full_sql_len = sql_len;
                            sql = NULL;
                        }
                    }
                    free(sql);
                }
                break;
            } else if (first_byte == MYSQL_COM_STMT_PREPARE) {
                if (payload_len > 1) {
                    pending_sql = malloc(payload_len);
                    if (pending_sql) {
                        size_t sql_len = payload_len - 1;
                        memcpy(pending_sql, packet_payload + 1, sql_len);
                        pending_sql[sql_len] = '\0';
                    }
                }
            } else if (first_byte == 0x00 && pending_sql && packet_num == 1) {
                if (payload_len >= 12) {
                    uint32_t stmt_id = packet_payload[1] | (packet_payload[2] << 8) |
                                       (packet_payload[3] << 16) | (packet_payload[4] << 24);
                    uint16_t num_params = packet_payload[9] | (packet_payload[10] << 8);

                    stmt_cache_t *cache = get_stmt_cache();
                    stmt_cache_put(cache, stmt_id, pending_sql, num_params);
                    full_sql = strdup(pending_sql);
                    full_sql_len = strlen(pending_sql);
                }
                free(pending_sql);
                pending_sql = NULL;
                break;
            }
        }

        if (packet_payload[0] == 0xFF) {
            break;
        }

        offset += 4 + packet_len;
    }

    free(pending_sql);
    pthread_mutex_unlock(&stream->mutex);

    if (full_sql && full_sql_len > 0) {
        for (size_t i = 0; i < full_sql_len; i++) {
            if (!isprint((unsigned char)full_sql[i]) && !isspace((unsigned char)full_sql[i])) {
                if (i < full_sql_len - 1 || full_sql[i] != '\0') {
                    full_sql[i] = ' ';
                }
            }
        }
    }

    return full_sql;
}

int parse_mysql_packet(const uint8_t *data, size_t len, char **sql_out) {
    if (!data || !sql_out) return -1;
    *sql_out = NULL;

    if (len < 4) return -1;

    uint8_t command = data[4];

    if (command == MYSQL_COM_QUERY || command == MYSQL_COM_STMT_PREPARE) {
        *sql_out = extract_query_from_packet(data + 4, len - 4, data);
        return *sql_out ? 0 : -1;
    }

    if (command == MYSQL_COM_STMT_EXECUTE) {
        return parse_com_stmt_execute(data + 5, len - 5, sql_out);
    }

    return -1;
}
