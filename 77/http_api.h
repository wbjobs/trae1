#ifndef VSHAPER_HTTP_API_H
#define VSHAPER_HTTP_API_H

#include "common.h"

typedef struct {
    int             server_fd;
    int             port;
    int             running;
    pthread_t       thread;
    pthread_mutex_t lock;
} http_api_server_t;

int  http_api_init(http_api_server_t *server, int port);
void http_api_start(http_api_server_t *server);
void http_api_stop(http_api_server_t *server);
void http_api_destroy(http_api_server_t *server);

#endif
