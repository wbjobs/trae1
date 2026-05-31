#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <stdbool.h>

#define MYSQL_PORT 3306
#define SNAP_LEN 65535
#define MAX_SQL_LEN 4096
#define MAX_USER_LEN 64
#define MAX_HOST_LEN 64
#define MAX_FILTER_LEN 256
#define SESSION_TABLE_SIZE 1024
#define TCP_WINDOW 65535

typedef enum {
    FMT_TEXT = 0,
    FMT_JSON,
    FMT_SYSLOG
} output_format_t;

typedef enum {
    SQL_TYPE_UNKNOWN = 0,
    SQL_TYPE_SELECT,
    SQL_TYPE_INSERT,
    SQL_TYPE_UPDATE,
    SQL_TYPE_DELETE,
    SQL_TYPE_REPLACE,
    SQL_TYPE_CREATE,
    SQL_TYPE_ALTER,
    SQL_TYPE_DROP,
    SQL_TYPE_TRUNCATE,
    SQL_TYPE_GRANT,
    SQL_TYPE_SET,
    SQL_TYPE_SHOW,
    SQL_TYPE_USE,
    SQL_TYPE_BEGIN,
    SQL_TYPE_COMMIT,
    SQL_TYPE_ROLLBACK,
    SQL_TYPE_CALL,
    SQL_TYPE_PREPARE,
    SQL_TYPE_EXECUTE,
    SQL_TYPE_OTHER
} sql_type_t;

typedef struct {
    char timestamp[64];
    char client_ip[64];
    uint16_t client_port;
    char user[MAX_USER_LEN];
    char database[MAX_USER_LEN];
    sql_type_t sql_type;
    char sql[MAX_SQL_LEN];
    double execution_time_ms;
    uint64_t affected_rows;
    uint64_t matched_rows;
    uint16_t warning_count;
    uint8_t has_error;
    uint16_t error_code;
    char error_message[256];
} mysql_event_t;

typedef struct {
    output_format_t format;
    char output_path[1024];
    char interface[256];
    char filter_sql_types[256];
    char filter_user[256];
    char filter_client_ip[256];
    bool case_sensitive;
    int  snaplen;
    int  promisc;
    int  timeout_ms;
    bool verbose;
    bool daemonize;
    char pid_file[1024];
    bool use_syslog;
    int  syslog_facility;
    char syslog_ident[64];
    size_t buffer_size;

    double slow_threshold_ms;
    char   slack_webhook_url[2048];
    char   dingtalk_webhook_url[2048];
    size_t ring_buffer_size;
    bool   alert_enabled;
} config_t;

#endif
