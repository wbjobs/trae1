#ifndef SESSION_H
#define SESSION_H

#include "common.h"
#include "crypto.h"
#include "auth.h"
#include "lru_cache.h"
#include "entropy.h"
#include <time.h>

typedef struct {
    int id;
    bool active;
    char client_ip[64];
    char username[MAX_USERNAME];
    time_t start_time;
    time_t last_activity;
    CryptoContext crypto;
    AuthContext auth;
    pthread_t thread_id;
    int current_credits;
    int max_credits;
    LruCache read_cache;
    LruCache write_cache;
    EntropyMonitor entropy_monitor;
    bool quarantined;
} Session;

typedef struct {
    Session sessions[MAX_SESSIONS];
    pthread_mutex_t lock;
    int active_count;
} SessionManager;

int session_manager_init(SessionManager *manager);
int session_create(SessionManager *manager, const char *client_ip, const char *username);
int session_get(SessionManager *manager, int session_id, Session **session);
void session_release(SessionManager *manager, int session_id);
void session_manager_cleanup(SessionManager *manager);
int session_get_active_count(SessionManager *manager);
int session_grant_credits(Session *session, int requested);
bool session_has_credits(Session *session, int required);
int session_quarantine(Session *session);

#endif
