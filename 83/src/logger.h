#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include <sqlite3.h>

typedef struct {
    sqlite3 *db;
    char session_id[64];
    char db_path[512];
    bool initialized;
} AuditLogger;

bool logger_init(AuditLogger *logger, const char *db_path);
void logger_close(AuditLogger *logger);
bool logger_start_session(AuditLogger *logger, const char *client_ip,
                          const char *username, const char *server_host, int server_port);
void logger_end_session(AuditLogger *logger);
bool logger_log_keyboard(AuditLogger *logger, uint32_t key, bool pressed);
bool logger_log_mouse(AuditLogger *logger, int x, int y, uint8_t button_mask);
bool logger_log_screenshot(AuditLogger *logger, const char *file_path);

#endif
