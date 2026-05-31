#include "logger.h"

const char* log_action_to_string(LogAction action) {
    switch (action) {
        case LOG_ACTION_READ: return "READ";
        case LOG_ACTION_WRITE: return "WRITE";
        case LOG_ACTION_DELETE: return "DELETE";
        case LOG_ACTION_CREATE: return "CREATE";
        case LOG_ACTION_OPEN: return "OPEN";
        case LOG_ACTION_CLOSE: return "CLOSE";
        case LOG_ACTION_LOGIN: return "LOGIN";
        case LOG_ACTION_LOGOUT: return "LOGOUT";
        default: return "UNKNOWN";
    }
}

int logger_init(Logger *logger, const char *db_path) {
    memset(logger, 0, sizeof(Logger));

    if (pthread_mutex_init(&logger->lock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize logger mutex\n");
        return -1;
    }

    int rc = sqlite3_open(db_path, &logger->db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(logger->db));
        pthread_mutex_destroy(&logger->lock);
        return -1;
    }

    const char *create_table_sql = 
        "CREATE TABLE IF NOT EXISTS access_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "username TEXT NOT NULL,"
        "client_ip TEXT NOT NULL,"
        "action TEXT NOT NULL,"
        "file_path TEXT"
        ");";

    char *err_msg = NULL;
    rc = sqlite3_exec(logger->db, create_table_sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(logger->db);
        pthread_mutex_destroy(&logger->lock);
        return -1;
    }

    logger->initialized = true;
    return 0;
}

int logger_log_access(Logger *logger, const char *username, const char *client_ip,
                       LogAction action, const char *file_path) {
    if (!logger->initialized) {
        return -1;
    }

    pthread_mutex_lock(&logger->lock);

    const char *insert_sql = 
        "INSERT INTO access_logs (username, client_ip, action, file_path) VALUES (?, ?, ?, ?);";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(logger->db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(logger->db));
        pthread_mutex_unlock(&logger->lock);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, client_ip, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, log_action_to_string(action), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, file_path ? file_path : "", -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to insert log: %s\n", sqlite3_errmsg(logger->db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&logger->lock);
        return -1;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&logger->lock);

    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    printf("[%s] %s@%s %s %s\n", time_str, username, client_ip, 
           log_action_to_string(action), file_path ? file_path : "");

    return 0;
}

void logger_cleanup(Logger *logger) {
    if (logger->initialized) {
        pthread_mutex_lock(&logger->lock);
        if (logger->db) {
            sqlite3_close(logger->db);
            logger->db = NULL;
        }
        pthread_mutex_unlock(&logger->lock);
        pthread_mutex_destroy(&logger->lock);
        logger->initialized = false;
    }
}
