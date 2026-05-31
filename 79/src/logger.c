#include "logger.h"
#include "compat.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE       *g_logfile = NULL;
static log_level_t g_level   = LOG_LEVEL_INFO;
static const char *level_str[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};

int logger_init(const char *logfile)
{
    if (logfile && logfile[0]) {
        g_logfile = fopen(logfile, "a");
        if (!g_logfile) {
            fprintf(stderr, "[LOG] Failed to open log file: %s\n", logfile);
            return -1;
        }
    }
    return 0;
}

void logger_close(void)
{
    if (g_logfile) {
        fclose(g_logfile);
        g_logfile = NULL;
    }
}

void logger_set_level(log_level_t level)
{
    g_level = level;
}

void logger_write(log_level_t level, const char *tag, const char *fmt, ...)
{
    if (level < g_level) return;

    va_list args;
    char    buffer[LOG_MAX_LEN];
    char    timestamp[64] = "";

    if (LOG_TIMESTAMP) {
        time_t    now = time(NULL);
        struct tm tm_info;
        localtime_s(&tm_info, &now);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);
    }

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (timestamp[0]) {
        fprintf(stdout, "[%s] [%s] %s\n", timestamp, level_str[level], buffer);
        fflush(stdout);
    }

    if (g_logfile) {
        if (timestamp[0]) {
            fprintf(g_logfile, "[%s] [%s] %s\n", timestamp, level_str[level], buffer);
        } else {
            fprintf(g_logfile, "[%s] %s\n", level_str[level], buffer);
        }
        fflush(g_logfile);
    }
}

void logger_device(const char *sn, const char *status, const char *detail)
{
    char    timestamp[64];
    time_t  now = time(NULL);
    struct tm tm_info;
    localtime_s(&tm_info, &now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);

    if (g_logfile) {
        fprintf(g_logfile, "[%s] [DEVICE] SN=%s  STATUS=%s  %s\n",
                timestamp, sn ? sn : "UNKNOWN", status, detail ? detail : "");
        fflush(g_logfile);
    }
    printf("[DEVICE] SN=%s  STATUS=%s  %s\n", sn ? sn : "UNKNOWN", status, detail ? detail : "");
}
