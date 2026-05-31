#include "nvme_hotplug_cli.h"

int init_pending_io_queue(io_queue_t *queue) {
    if (!queue) return -1;

    memset(queue, 0, sizeof(io_queue_t));
    pthread_mutex_init(&queue->mutex, NULL);
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->next_io_id = 1;
    return 0;
}

void destroy_pending_io_queue(io_queue_t *queue) {
    if (!queue) return;

    pthread_mutex_lock(&queue->mutex);

    pending_io_t *current = queue->head;
    while (current) {
        pending_io_t *next = current->next;
        free(current);
        current = next;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;

    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);
}

pending_io_t* submit_pending_io(io_queue_t *queue, void *buffer, size_t size,
                                off_t offset, int type, io_callback_t callback, void *arg) {
    if (!queue) return NULL;

    pthread_mutex_lock(&queue->mutex);

    if (queue->count >= MAX_PENDING_IO) {
        pthread_mutex_unlock(&queue->mutex);
        log_to_syslog(LOG_WARN, "Pending I/O queue full, rejecting new I/O");
        return NULL;
    }

    pending_io_t *io = (pending_io_t *)calloc(1, sizeof(pending_io_t));
    if (!io) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }

    io->io_id = queue->next_io_id++;
    io->buffer = buffer;
    io->size = size;
    io->offset = offset;
    io->type = type;
    io->state = IO_STATE_PENDING;
    io->callback = callback;
    io->callback_arg = arg;
    io->submit_time = time(NULL);
    io->next = NULL;

    if (queue->tail) {
        queue->tail->next = io;
        queue->tail = io;
    } else {
        queue->head = queue->tail = io;
    }

    queue->count++;

    pthread_mutex_unlock(&queue->mutex);

    return io;
}

int complete_pending_io(io_queue_t *queue, uint64_t io_id, int status) {
    if (!queue) return -1;

    pthread_mutex_lock(&queue->mutex);

    pending_io_t *current = queue->head;
    pending_io_t *prev = NULL;

    while (current) {
        if (current->io_id == io_id) {
            current->state = (status == 0) ? IO_STATE_COMPLETED : IO_STATE_FAILED;

            if (current->callback) {
                pthread_mutex_unlock(&queue->mutex);
                current->callback(current->callback_arg, status);
                pthread_mutex_lock(&queue->mutex);
            }

            if (prev) {
                prev->next = current->next;
            } else {
                queue->head = current->next;
            }

            if (current == queue->tail) {
                queue->tail = prev;
            }

            queue->count--;
            free(current);

            pthread_mutex_unlock(&queue->mutex);
            return 0;
        }
        prev = current;
        current = current->next;
    }

    pthread_mutex_unlock(&queue->mutex);
    return -1;
}

int cancel_all_pending_io(io_queue_t *queue, int status) {
    if (!queue) return -1;

    pthread_mutex_lock(&queue->mutex);

    pending_io_t *current = queue->head;
    int cancelled = 0;

    while (current) {
        pending_io_t *next = current->next;

        current->state = IO_STATE_CANCELLED;

        if (current->callback) {
            pthread_mutex_unlock(&queue->mutex);
            current->callback(current->callback_arg, status);
            pthread_mutex_lock(&queue->mutex);
        }

        free(current);
        cancelled++;

        current = next;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;

    pthread_mutex_unlock(&queue->mutex);

    log_to_syslog(LOG_INFO, "Cancelled %d pending I/O operations with status %d", cancelled, status);

    return cancelled;
}

int abort_stale_pending_io(io_queue_t *queue, int timeout_sec) {
    if (!queue) return 0;

    pthread_mutex_lock(&queue->mutex);

    time_t now = time(NULL);
    pending_io_t *current = queue->head;
    pending_io_t *prev = NULL;
    int aborted = 0;

    while (current) {
        pending_io_t *next = current->next;

        if ((now - current->submit_time) >= timeout_sec) {
            current->state = IO_STATE_FAILED;

            if (current->callback) {
                pthread_mutex_unlock(&queue->mutex);
                current->callback(current->callback_arg, -ETIMEDOUT);
                pthread_mutex_lock(&queue->mutex);
            }

            if (prev) {
                prev->next = next;
            } else {
                queue->head = next;
            }

            if (current == queue->tail) {
                queue->tail = prev;
            }

            free(current);
            aborted++;
            queue->count--;
        } else {
            prev = current;
        }

        current = next;
    }

    pthread_mutex_unlock(&queue->mutex);

    if (aborted > 0) {
        log_to_syslog(LOG_WARN, "Aborted %d stale pending I/O operations", aborted);
    }

    return aborted;
}

int get_pending_io_count(io_queue_t *queue) {
    if (!queue) return 0;
    pthread_mutex_lock(&queue->mutex);
    int count = queue->count;
    pthread_mutex_unlock(&queue->mutex);
    return count;
}
