#include "session.h"

int session_manager_init(SessionManager *manager) {
    memset(manager, 0, sizeof(SessionManager));
    if (pthread_mutex_init(&manager->lock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize session mutex\n");
        return -1;
    }

    for (int i = 0; i < MAX_SESSIONS; i++) {
        manager->sessions[i].id = i;
        manager->sessions[i].active = false;
    }

    return 0;
}

int session_create(SessionManager *manager, const char *client_ip, const char *username) {
    pthread_mutex_lock(&manager->lock);

    if (manager->active_count >= MAX_SESSIONS) {
        fprintf(stderr, "Maximum sessions reached (%d)\n", MAX_SESSIONS);
        pthread_mutex_unlock(&manager->lock);
        return -1;
    }

    int session_id = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!manager->sessions[i].active) {
            session_id = i;
            break;
        }
    }

    if (session_id == -1) {
        pthread_mutex_unlock(&manager->lock);
        return -1;
    }

    Session *session = &manager->sessions[session_id];
    memset(session, 0, sizeof(Session));
    session->id = session_id;
    session->active = true;
    strncpy(session->client_ip, client_ip, 63);
    strncpy(session->username, username, MAX_USERNAME - 1);
    session->start_time = time(NULL);
    session->last_activity = time(NULL);

    session->max_credits = global_config.max_credits;
    session->current_credits = global_config.max_credits;
    session->quarantined = false;

    if (entropy_monitor_init(&session->entropy_monitor) != 0) {
        fprintf(stderr, "Warning: Failed to initialize entropy monitor\n");
    }

    if (global_config.encrypt) {
        if (crypto_init(&session->crypto) != 0) {
            session->active = false;
            pthread_mutex_unlock(&manager->lock);
            return -1;
        }
    }

    size_t per_session_cache = global_config.cache_size / MAX_SESSIONS;
    if (per_session_cache < MIN_CACHE_SIZE) {
        per_session_cache = MIN_CACHE_SIZE;
    }

    if (lru_cache_init(&session->read_cache, per_session_cache, 1024) != 0) {
        fprintf(stderr, "Failed to initialize read cache for session %d\n", session_id);
    }

    if (lru_cache_init(&session->write_cache, per_session_cache / 2, 512) != 0) {
        fprintf(stderr, "Failed to initialize write cache for session %d\n", session_id);
    }

    manager->active_count++;
    pthread_mutex_unlock(&manager->lock);

    printf("Session %d: credits=%d, read_cache=%zu MB, write_cache=%zu MB\n",
           session_id, session->max_credits,
           per_session_cache / (1024 * 1024),
           per_session_cache / (2 * 1024 * 1024));

    return session_id;
}

int session_get(SessionManager *manager, int session_id, Session **session) {
    pthread_mutex_lock(&manager->lock);

    if (session_id < 0 || session_id >= MAX_SESSIONS || !manager->sessions[session_id].active) {
        pthread_mutex_unlock(&manager->lock);
        return -1;
    }

    *session = &manager->sessions[session_id];
    (*session)->last_activity = time(NULL);

    return 0;
}

void session_release(SessionManager *manager, int session_id) {
    pthread_mutex_lock(&manager->lock);

    if (session_id >= 0 && session_id < MAX_SESSIONS && manager->sessions[session_id].active) {
        Session *session = &manager->sessions[session_id];
        
        lru_cache_flush(&session->write_cache, global_config.local_path);
        lru_cache_destroy(&session->read_cache);
        lru_cache_destroy(&session->write_cache);
        entropy_monitor_destroy(&session->entropy_monitor);
        
        crypto_cleanup(&session->crypto);
        auth_cleanup(&session->auth);
        memset(session, 0, sizeof(Session));
        session->id = session_id;
        session->active = false;
        manager->active_count--;
    }

    pthread_mutex_unlock(&manager->lock);
}

void session_manager_cleanup(SessionManager *manager) {
    pthread_mutex_lock(&manager->lock);

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (manager->sessions[i].active) {
            lru_cache_flush(&manager->sessions[i].write_cache, global_config.local_path);
            lru_cache_destroy(&manager->sessions[i].read_cache);
            lru_cache_destroy(&manager->sessions[i].write_cache);
            entropy_monitor_destroy(&manager->sessions[i].entropy_monitor);
            crypto_cleanup(&manager->sessions[i].crypto);
            auth_cleanup(&manager->sessions[i].auth);
            manager->sessions[i].active = false;
        }
    }

    manager->active_count = 0;
    pthread_mutex_unlock(&manager->lock);
    pthread_mutex_destroy(&manager->lock);
}

int session_get_active_count(SessionManager *manager) {
    pthread_mutex_lock(&manager->lock);
    int count = manager->active_count;
    pthread_mutex_unlock(&manager->lock);
    return count;
}

int session_grant_credits(Session *session, int requested) {
    if (!session || requested <= 0) {
        return -1;
    }

    if (session->current_credits + requested > session->max_credits) {
        int granted = session->max_credits - session->current_credits;
        session->current_credits = session->max_credits;
        return granted;
    }

    session->current_credits += requested;
    return requested;
}

bool session_has_credits(Session *session, int required) {
    if (!session || required <= 0) {
        return false;
    }
    return session->current_credits >= required;
}

int session_quarantine(Session *session) {
    if (!session) {
        return -1;
    }
    session->quarantined = true;
    return 0;
}
