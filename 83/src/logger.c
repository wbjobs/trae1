#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void generate_session_id(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_part[32];
    strftime(time_part, sizeof(time_part), "%Y%m%d_%H%M%S", tm_info);
    snprintf(buf, len, "sess_%s_%06d", time_part, rand() % 1000000);
}

bool logger_init(AuditLogger *logger, const char *db_path)
{
    memset(logger, 0, sizeof(*logger));
    strncpy(logger->db_path, db_path, sizeof(logger->db_path) - 1);

    int rc = sqlite3_open(db_path, &logger->db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(logger->db));
        sqlite3_close(logger->db);
        return false;
    }

    const char *create_tables =
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id TEXT NOT NULL UNIQUE,"
        "  client_ip TEXT,"
        "  username TEXT,"
        "  server_host TEXT,"
        "  server_port INTEGER,"
        "  start_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  end_time TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS keyboard_events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id TEXT NOT NULL,"
        "  timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  key_code INTEGER,"
        "  pressed INTEGER,"
        "  FOREIGN KEY (session_id) REFERENCES sessions(session_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS mouse_events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id TEXT NOT NULL,"
        "  timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  x INTEGER,"
        "  y INTEGER,"
        "  button_mask INTEGER,"
        "  FOREIGN KEY (session_id) REFERENCES sessions(session_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS screenshots ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id TEXT NOT NULL,"
        "  timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  file_path TEXT,"
        "  FOREIGN KEY (session_id) REFERENCES sessions(session_id)"
        ");";

    char *err_msg = NULL;
    rc = sqlite3_exec(logger->db, create_tables, NULL, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(logger->db);
        return false;
    }

    srand((unsigned int)time(NULL));
    logger->initialized = true;
    return true;
}

void logger_close(AuditLogger *logger)
{
    if (logger->db) {
        sqlite3_close(logger->db);
        logger->db = NULL;
    }
    logger->initialized = false;
}

bool logger_start_session(AuditLogger *logger, const char *client_ip,
                          const char *username, const char *server_host, int server_port)
{
    if (!logger->initialized) return false;

    generate_session_id(logger->session_id, sizeof(logger->session_id));

    const char *sql = "INSERT INTO sessions (session_id, client_ip, username, server_host, server_port) "
                      "VALUES (?, ?, ?, ?, ?)";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(logger->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, logger->session_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, client_ip ? client_ip : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, username ? username : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, server_host ? server_host : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, server_port);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

void logger_end_session(AuditLogger *logger)
{
    if (!logger->initialized || logger->session_id[0] == '\0') return;

    const char *sql = "UPDATE sessions SET end_time = CURRENT_TIMESTAMP WHERE session_id = ?";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(logger->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, logger->session_id, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    logger->session_id[0] = '\0';
}

bool logger_log_keyboard(AuditLogger *logger, uint32_t key, bool pressed)
{
    if (!logger->initialized || logger->session_id[0] == '\0') return false;

    const char *sql = "INSERT INTO keyboard_events (session_id, key_code, pressed) VALUES (?, ?, ?)";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(logger->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, logger->session_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, (int)key);
    sqlite3_bind_int(stmt, 3, pressed ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool logger_log_mouse(AuditLogger *logger, int x, int y, uint8_t button_mask)
{
    if (!logger->initialized || logger->session_id[0] == '\0') return false;

    const char *sql = "INSERT INTO mouse_events (session_id, x, y, button_mask) VALUES (?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(logger->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, logger->session_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, x);
    sqlite3_bind_int(stmt, 3, y);
    sqlite3_bind_int(stmt, 4, (int)button_mask);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool logger_log_screenshot(AuditLogger *logger, const char *file_path)
{
    if (!logger->initialized || logger->session_id[0] == '\0') return false;

    const char *sql = "INSERT INTO screenshots (session_id, file_path) VALUES (?, ?)";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(logger->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, logger->session_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, file_path ? file_path : "", -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}
