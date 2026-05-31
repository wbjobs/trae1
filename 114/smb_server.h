#ifndef SMB_SERVER_H
#define SMB_SERVER_H

#include "common.h"
#include "session.h"
#include "logger.h"
#include "auth.h"
#include "ransomware_detector.h"
#include "behavior_learning.h"

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define SMB_PORT 445
#define BUFFER_SIZE 65536

typedef struct {
    int server_socket;
    bool running;
    SessionManager session_manager;
    Logger logger;
    AuthContext auth_context;
    RansomwareDetector ransomware_detector;
    BehaviorLearner behavior_learner;
    pthread_t accept_thread;
} SmbServer;

extern SmbServer *global_server;

int smb_server_init(SmbServer *server);
int smb_server_start(SmbServer *server);
void smb_server_stop(SmbServer *server);
void smb_server_cleanup(SmbServer *server);

#endif
