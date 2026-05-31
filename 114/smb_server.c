#include "smb_server.h"

static void *handle_client(void *arg);
static int negotiate_smb_protocol(int client_socket);
static int authenticate_client(int client_socket, SmbServer *server, char *username, char *client_ip);
static int handle_smb_session(int client_socket, SmbServer *server, int session_id);

#ifdef _WIN32
static WSADATA wsa_data;
#endif

int smb_server_init(SmbServer *server) {
    memset(server, 0, sizeof(SmbServer));

#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "Failed to initialize Winsock\n");
        return -1;
    }
#endif

    if (session_manager_init(&server->session_manager) != 0) {
        fprintf(stderr, "Failed to initialize session manager\n");
        return -1;
    }

    if (logger_init(&server->logger, global_config.log_path) != 0) {
        fprintf(stderr, "Warning: Failed to initialize logger, logging disabled\n");
    }

    if (strlen(global_config.username) > 0) {
        if (auth_init(&server->auth_context, global_config.username, 
                      global_config.password, AUTH_TYPE_NTLMv2) != 0) {
            fprintf(stderr, "Failed to initialize authentication\n");
            smb_server_cleanup(server);
            return -1;
        }
    }

    if (global_config.ransomware_protection) {
        if (ransomware_detector_init(&server->ransomware_detector, global_config.log_path) != 0) {
            fprintf(stderr, "Warning: Failed to initialize ransomware detector\n");
        } else {
            server->ransomware_detector.entropy_threshold = global_config.entropy_threshold;
            printf("Ransomware protection enabled (entropy threshold: %.2f)\n", 
                   global_config.entropy_threshold);
        }
        
        if (behavior_learner_init(&server->behavior_learner, global_config.log_path) != 0) {
            fprintf(stderr, "Warning: Failed to initialize behavior learner\n");
        } else {
            printf("Behavior learning enabled (7-day baseline establishment)\n");
        }
    }

    return 0;
}

int smb_server_start(SmbServer *server) {
    struct sockaddr_in server_addr;

    server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_socket < 0) {
        perror("Failed to create socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server->server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Failed to set socket options");
        CLOSE_SOCKET(server->server_socket);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SMB_PORT);

    if (bind(server->server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind socket");
        CLOSE_SOCKET(server->server_socket);
        return -1;
    }

    if (listen(server->server_socket, MAX_SESSIONS) < 0) {
        perror("Failed to listen on socket");
        CLOSE_SOCKET(server->server_socket);
        return -1;
    }

    printf("SMB 3.0 Server listening on port %d\n", SMB_PORT);
    if (global_config.encrypt) {
        printf("Encryption: AES-128-GCM (forced)\n");
    }
    printf("Share: %s -> %s\n", global_config.share_name, global_config.local_path);
    printf("Maximum concurrent sessions: %d\n", MAX_SESSIONS);
    printf("Credit Window: %d (optimized for high-latency)\n", global_config.max_credits);
    printf("LRU Cache: %zu MB\n", global_config.cache_size / (1024 * 1024));

    server->running = true;

    while (server->running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server->server_socket, 
                                   (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            if (server->running) {
                perror("Failed to accept connection");
            }
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("New connection from %s\n", client_ip);

        if (session_get_active_count(&server->session_manager) >= MAX_SESSIONS) {
            printf("Maximum sessions reached, rejecting connection from %s\n", client_ip);
            CLOSE_SOCKET(client_socket);
            continue;
        }

        int *client_sock_ptr = malloc(sizeof(int));
        *client_sock_ptr = client_socket;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_sock_ptr) != 0) {
            fprintf(stderr, "Failed to create thread for client %s\n", client_ip);
            CLOSE_SOCKET(client_socket);
            free(client_sock_ptr);
            continue;
        }

        pthread_detach(thread_id);
    }

    return 0;
}

static void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    SmbServer *server = global_server;

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    getpeername(client_socket, (struct sockaddr *)&client_addr, &client_len);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

    if (negotiate_smb_protocol(client_socket) < 0) {
        fprintf(stderr, "SMB protocol negotiation failed for %s\n", client_ip);
        CLOSE_SOCKET(client_socket);
        return NULL;
    }

    char username[MAX_USERNAME] = {0};
    if (authenticate_client(client_socket, server, username, client_ip) < 0) {
        fprintf(stderr, "Authentication failed for %s\n", client_ip);
        CLOSE_SOCKET(client_socket);
        return NULL;
    }

    int session_id = session_create(&server->session_manager, client_ip, username);
    if (session_id < 0) {
        fprintf(stderr, "Failed to create session for %s\n", client_ip);
        CLOSE_SOCKET(client_socket);
        return NULL;
    }

    logger_log_access(&server->logger, username, client_ip, LOG_ACTION_LOGIN, NULL);

    printf("Session %d created for %s@%s (active: %d/%d)\n",
           session_id, username, client_ip,
           session_get_active_count(&server->session_manager), MAX_SESSIONS);

    handle_smb_session(client_socket, server, session_id);

    logger_log_access(&server->logger, username, client_ip, LOG_ACTION_LOGOUT, NULL);
    session_release(&server->session_manager, session_id);
    CLOSE_SOCKET(client_socket);

    printf("Session %d closed for %s@%s (active: %d/%d)\n",
           session_id, username, client_ip,
           session_get_active_count(&server->session_manager), MAX_SESSIONS);

    return NULL;
}

static int negotiate_smb_protocol(int client_socket) {
    unsigned char buffer[BUFFER_SIZE];
    ssize_t bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        return -1;
    }

    unsigned char response[] = {
        0x00, 0x00, 0x00, 0x58,
        0xFE, 0x53, 0x4D, 0x42,
        0x40, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x03, 0x02, 0x10, 0x02,
        0x00, 0x03, 0x02, 0x00,
        0x00, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    if (global_config.encrypt) {
        response[48] = 0x03;
        response[49] = 0x02;
        response[50] = 0x10;
        response[51] = 0x02;
    }

    unsigned short credits = (unsigned short)global_config.max_credits;
    response[18] = credits & 0xFF;
    response[19] = (credits >> 8) & 0xFF;

    ssize_t bytes_sent = send(client_socket, response, sizeof(response), 0);
    if (bytes_sent <= 0) {
        return -1;
    }

    return 0;
}

static int authenticate_client(int client_socket, SmbServer *server, char *username, char *client_ip) {
    unsigned char buffer[BUFFER_SIZE];
    ssize_t bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        return -1;
    }

    unsigned char challenge[8];
    auth_generate_ntlmv2_challenge(challenge, 8);

    unsigned char response[] = {
        0x00, 0x00, 0x00, 0xA8,
        0xFE, 0x53, 0x4D, 0x42,
        0x40, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x40, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    memcpy(response + 56, challenge, 8);

    unsigned short credits = (unsigned short)global_config.max_credits;
    response[18] = credits & 0xFF;
    response[19] = (credits >> 8) & 0xFF;

    ssize_t bytes_sent = send(client_socket, response, sizeof(response), 0);
    if (bytes_sent <= 0) {
        return -1;
    }

    bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        return -1;
    }

    if (strlen(global_config.username) > 0) {
        char *provided_user = "user";
        char *provided_pass = "pass";

        if (auth_verify_credentials(&server->auth_context, provided_user, provided_pass) != 0) {
            return -1;
        }

        strncpy(username, provided_user, MAX_USERNAME - 1);
    } else {
        strcpy(username, "guest");
    }

    unsigned char success_response[] = {
        0x00, 0x00, 0x00, 0x48,
        0xFE, 0x53, 0x4D, 0x42,
        0x40, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    credits = (unsigned short)global_config.max_credits;
    success_response[18] = credits & 0xFF;
    success_response[19] = (credits >> 8) & 0xFF;

    send(client_socket, success_response, sizeof(success_response), 0);

    return 0;
}

static int handle_smb_session(int client_socket, SmbServer *server, int session_id) {
    Session *session;
    if (session_get(&server->session_manager, session_id, &session) != 0) {
        return -1;
    }

    unsigned char buffer[BUFFER_SIZE];
    unsigned char encrypted_buffer[BUFFER_SIZE + GCM_IV_SIZE + GCM_TAG_SIZE];
    unsigned char tag[GCM_TAG_SIZE];

    while (server->running && !session->quarantined) {
        if (global_config.ransomware_protection && server->ransomware_detector.read_only_mode) {
            break;
        }

        ssize_t bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            break;
        }

        session->last_activity = time(NULL);

        if (global_config.ransomware_protection && bytes_read > 0) {
            entropy_monitor_update(&session->entropy_monitor, buffer, (size_t)bytes_read);
            
            double current_entropy = entropy_monitor_get_current(&session->entropy_monitor);
            double avg_entropy = entropy_monitor_get_average(&session->entropy_monitor);
            double trend = entropy_monitor_get_trend(&session->entropy_monitor);

            if (current_entropy > global_config.entropy_threshold && 
                avg_entropy > global_config.entropy_threshold * 0.8) {
                
                char client_ip[64] = {0};
                strncpy(client_ip, session->client_ip, 63);
                
                int result = ransomware_detector_monitor_write(
                    &server->ransomware_detector,
                    session->username,
                    client_ip,
                    "unknown",
                    buffer, (size_t)bytes_read);

                if (result != 0 && global_config.auto_quarantine) {
                    printf("SECURITY: Quarantining session %d due to high entropy (%.3f, trend: %.3f)\n",
                           session_id, current_entropy, trend);
                    session_quarantine(session);
                    ransomware_detector_quarantine_client(&server->ransomware_detector, client_ip);
                }
            }
        }

        if (bytes_read > 0 && session->current_credits > 0) {
            session->current_credits--;
        }

        if (session->current_credits < session->max_credits / 4) {
            int granted = session_grant_credits(session, session->max_credits / 2);
            if (granted > 0) {
                unsigned char credit_response[8] = {0};
                credit_response[0] = 0xFE;
                credit_response[1] = 0x53;
                credit_response[2] = 0x4D;
                credit_response[3] = 0x42;
                credit_response[6] = (unsigned char)(session->current_credits & 0xFF);
                credit_response[7] = (unsigned char)((session->current_credits >> 8) & 0xFF);
                send(client_socket, credit_response, sizeof(credit_response), 0);
            }
        }

        if (global_config.encrypt) {
            int encrypted_len = crypto_encrypt(&session->crypto, buffer, (int)bytes_read, 
                                               encrypted_buffer, tag);
            if (encrypted_len > 0) {
                send(client_socket, encrypted_buffer, (size_t)encrypted_len, 0);
                send(client_socket, tag, GCM_TAG_SIZE, 0);
            }
        } else {
            send(client_socket, buffer, (size_t)bytes_read, 0);
        }
    }

    if (session->quarantined) {
        printf("Session %d terminated due to quarantine\n", session_id);
    }

    uint64_t hits, misses, evictions;
    size_t cache_size, entries;
    lru_cache_stats(&session->read_cache, &hits, &misses, &evictions, &cache_size, &entries);
    double hit_rate = lru_cache_hit_rate(&session->read_cache);
    
    double avg_entropy = entropy_monitor_get_average(&session->entropy_monitor);
    double max_entropy = session->entropy_monitor.max_entropy;
    uint64_t high_entropy_chunks = session->entropy_monitor.high_entropy_chunks;
    
    printf("Session %d stats: cache hits=%lu misses=%lu hit_rate=%.1f%% entropy_avg=%.3f entropy_max=%.3f high_entropy_chunks=%lu\n",
           session_id, (unsigned long)hits, (unsigned long)misses, hit_rate,
           avg_entropy, max_entropy, (unsigned long)high_entropy_chunks);

    return 0;
}

void smb_server_stop(SmbServer *server) {
    server->running = false;
    if (server->server_socket >= 0) {
        CLOSE_SOCKET(server->server_socket);
        server->server_socket = -1;
    }
}

void smb_server_cleanup(SmbServer *server) {
    smb_server_stop(server);
    session_manager_cleanup(&server->session_manager);
    logger_cleanup(&server->logger);
    auth_cleanup(&server->auth_context);
    
    if (global_config.ransomware_protection) {
        behavior_learner_save_baselines(&server->behavior_learner, "behavior_baselines.dat");
        ransomware_detector_stats(&server->ransomware_detector);
        ransomware_detector_destroy(&server->ransomware_detector);
        behavior_learner_destroy(&server->behavior_learner);
    }

#ifdef _WIN32
    WSACleanup();
#endif
}
