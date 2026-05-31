#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#define LOG_MAX_LEN    4096
#define LOG_TIMESTAMP  1

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

int  logger_init(const char *logfile);
void logger_close(void);
void logger_set_level(log_level_t level);
void logger_write(log_level_t level, const char *tag, const char *fmt, ...);
void logger_device(const char *sn, const char *status, const char *detail);

#define LOG_DEBUG(fmt, ...) logger_write(LOG_LEVEL_DEBUG, "DBG", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  logger_write(LOG_LEVEL_INFO,  "INF", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  logger_write(LOG_LEVEL_WARN,  "WRN", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) logger_write(LOG_LEVEL_ERROR, "ERR", fmt, ##__VA_ARGS__)

#endif
