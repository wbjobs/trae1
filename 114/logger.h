#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"
#include <sqlite3.h>
#include <time.h>

typedef enum {
    LOG_ACTION_READ,
    LOG_ACTION_WRITE,
    LOG_ACTION_DELETE,
    LOG_ACTION_CREATE,
    LOG_ACTION_OPEN,
    LOG_ACTION_CLOSE,
    LOG_ACTION_LOGIN,
    LOG_ACTION_LOGOUT
} LogAction;

typedef struct {
    sqlite3 *db;
    pthread_mutex_t lock;
    bool initialized;
} Logger;

int logger_init(Logger *logger, const char *db_path);
int logger_log_access(Logger *logger, const char *username, const char *client_ip,
                       LogAction action, const char *file_path);
void logger_cleanup(Logger *logger);
const char* log_action_to_string(LogAction action);

#endif
