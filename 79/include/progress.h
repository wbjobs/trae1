#ifndef PROGRESS_H
#define PROGRESS_H

#include <stdint.h>
#include <pthread.h>

#define MAX_DEVICES        10
#define PROGRESS_BAR_WIDTH 30

typedef enum {
    OP_IDLE = 0,
    OP_ERASE,
    OP_WRITE,
    OP_READ,
    OP_VERIFY,
    OP_BACKUP,
    OP_DISCONNECTED,
    OP_RECONNECTING,
    OP_DONE,
    OP_FAILED
} operation_t;

typedef struct {
    int         index;
    char        sn[64];
    uint32_t    total;
    uint32_t    current;
    int         percent;
    operation_t op;
    int         retry_count;
    int         reconnect_count;
    int         active;
    pthread_mutex_t mutex;
} device_progress_t;

int  progress_init(int device_count);
void progress_cleanup(void);
void progress_set_device(int index, const char *sn);
void progress_update(int index, operation_t op, uint32_t current, uint32_t total);
void progress_set_retry(int index, int retry);
void progress_set_reconnect(int index, int reconnect_attempt);
void progress_set_disconnected(int index);
void progress_set_reconnecting(int index);
void progress_set_failed(int index);
void progress_set_done(int index);
void progress_render(void);
void progress_render_line(int index);

const char *operation_str(operation_t op);

#endif
