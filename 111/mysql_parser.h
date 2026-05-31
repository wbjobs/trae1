#ifndef MYSQL_PARSER_H
#define MYSQL_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include "stmt_cache.h"

#define MYSQL_PACKET_HEADER_SIZE 4
#define MYSQL_MAX_PACKET_SIZE 0xFFFFFF
#define MAX_PARAMS 1024

typedef enum {
    MYSQL_COM_SLEEP = 0x00,
    MYSQL_COM_QUIT = 0x01,
    MYSQL_COM_INIT_DB = 0x02,
    MYSQL_COM_QUERY = 0x03,
    MYSQL_COM_FIELD_LIST = 0x04,
    MYSQL_COM_CREATE_DB = 0x05,
    MYSQL_COM_DROP_DB = 0x06,
    MYSQL_COM_REFRESH = 0x07,
    MYSQL_COM_SHUTDOWN = 0x08,
    MYSQL_COM_STATISTICS = 0x09,
    MYSQL_COM_PROCESS_INFO = 0x0A,
    MYSQL_COM_CONNECT = 0x0B,
    MYSQL_COM_PROCESS_KILL = 0x0C,
    MYSQL_COM_DEBUG = 0x0D,
    MYSQL_COM_PING = 0x0E,
    MYSQL_COM_TIME = 0x0F,
    MYSQL_COM_DELAYED_INSERT = 0x10,
    MYSQL_COM_CHANGE_USER = 0x11,
    MYSQL_COM_BINLOG_DUMP = 0x12,
    MYSQL_COM_TABLE_DUMP = 0x13,
    MYSQL_COM_CONNECT_OUT = 0x14,
    MYSQL_COM_REGISTER_SLAVE = 0x15,
    MYSQL_COM_STMT_PREPARE = 0x16,
    MYSQL_COM_STMT_EXECUTE = 0x17,
    MYSQL_COM_STMT_SEND_LONG_DATA = 0x18,
    MYSQL_COM_STMT_CLOSE = 0x19,
    MYSQL_COM_STMT_RESET = 0x1A,
    MYSQL_COM_SET_OPTION = 0x1B,
    MYSQL_COM_STMT_FETCH = 0x1C,
    MYSQL_COM_DAEMON = 0x1D,
    MYSQL_COM_BINLOG_DUMP_GTID = 0x1E,
    MYSQL_COM_RESET_CONNECTION = 0x1F
} mysql_command_t;

typedef enum {
    MYSQL_TYPE_DECIMAL = 0x00,
    MYSQL_TYPE_TINY = 0x01,
    MYSQL_TYPE_SHORT = 0x02,
    MYSQL_TYPE_LONG = 0x03,
    MYSQL_TYPE_FLOAT = 0x04,
    MYSQL_TYPE_DOUBLE = 0x05,
    MYSQL_TYPE_NULL = 0x06,
    MYSQL_TYPE_TIMESTAMP = 0x07,
    MYSQL_TYPE_LONGLONG = 0x08,
    MYSQL_TYPE_INT24 = 0x09,
    MYSQL_TYPE_DATE = 0x0A,
    MYSQL_TYPE_TIME = 0x0B,
    MYSQL_TYPE_DATETIME = 0x0C,
    MYSQL_TYPE_YEAR = 0x0D,
    MYSQL_TYPE_NEWDATE = 0x0E,
    MYSQL_TYPE_VARCHAR = 0x0F,
    MYSQL_TYPE_BIT = 0x10,
    MYSQL_TYPE_TIMESTAMP2 = 0x11,
    MYSQL_TYPE_DATETIME2 = 0x12,
    MYSQL_TYPE_TIME2 = 0x13,
    MYSQL_TYPE_NEWDECIMAL = 0xF6,
    MYSQL_TYPE_ENUM = 0xF7,
    MYSQL_TYPE_SET = 0xF8,
    MYSQL_TYPE_TINY_BLOB = 0xF9,
    MYSQL_TYPE_MEDIUM_BLOB = 0xFA,
    MYSQL_TYPE_LONG_BLOB = 0xFB,
    MYSQL_TYPE_BLOB = 0xFC,
    MYSQL_TYPE_VAR_STRING = 0xFD,
    MYSQL_TYPE_STRING = 0xFE,
    MYSQL_TYPE_GEOMETRY = 0xFF
} mysql_type_t;

typedef struct mysql_param {
    uint8_t type;
    uint8_t is_null;
    uint8_t is_unsigned;
    union {
        int64_t int_val;
        uint64_t uint_val;
        double double_val;
        float float_val;
        struct {
            uint8_t *data;
            uint32_t len;
        } str_val;
    } value;
} mysql_param_t;

stmt_cache_t *get_stmt_cache(void);
char *extract_mysql_sql(void *tcp_stream);
int parse_mysql_packet(const uint8_t *data, size_t len, char **sql_out);
int is_mysql_packet(const uint8_t *data, size_t len);

#endif
