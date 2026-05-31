#include "progress.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#include <term.h>
#endif

static device_progress_t *g_devices = NULL;
static int                g_count   = 0;

#ifdef _WIN32
static CONSOLE_SCREEN_BUFFER_INFO g_csbi;
#endif
static int                g_start_line = 0;

const char *operation_str(operation_t op)
{
    switch (op) {
        case OP_IDLE:         return "IDLE     ";
        case OP_ERASE:        return "ERASE    ";
        case OP_WRITE:        return "WRITE    ";
        case OP_READ:         return "READ     ";
        case OP_VERIFY:       return "VERIFY   ";
        case OP_BACKUP:       return "BACKUP   ";
        case OP_DISCONNECTED: return "DISCONN  ";
        case OP_RECONNECTING: return "RECONN   ";
        case OP_DONE:         return "DONE     ";
        case OP_FAILED:       return "FAILED   ";
        default:              return "???      ";
    }
}

#ifdef _WIN32
static WORD get_op_color(operation_t op)
{
    switch (op) {
        case OP_ERASE:        return 11;
        case OP_WRITE:        return 14;
        case OP_READ:         return 9;
        case OP_VERIFY:       return 13;
        case OP_BACKUP:       return 3;
        case OP_DISCONNECTED: return 12;
        case OP_RECONNECTING: return 14;
        case OP_DONE:         return 10;
        case OP_FAILED:       return 12;
        default:              return 7;
    }
}

static void set_cursor_pos(int x, int y)
{
    COORD pos = { (SHORT)x, (SHORT)y };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
}
#else
static const char *get_op_color_ansi(operation_t op)
{
    switch (op) {
        case OP_ERASE:        return "\033[36m";
        case OP_WRITE:        return "\033[33m";
        case OP_READ:         return "\033[34m";
        case OP_VERIFY:       return "\033[35m";
        case OP_BACKUP:       return "\033[36m";
        case OP_DISCONNECTED: return "\033[31m";
        case OP_RECONNECTING: return "\033[33m";
        case OP_DONE:         return "\033[32m";
        case OP_FAILED:       return "\033[31m";
        default:              return "\033[37m";
    }
}

static void set_cursor_pos(int x, int y)
{
    printf("\033[%d;%dH", y + 1, x + 1);
}
#endif

int progress_init(int device_count)
{
    g_count = (device_count > MAX_DEVICES) ? MAX_DEVICES : device_count;
    g_devices = (device_progress_t *)calloc((size_t)g_count, sizeof(device_progress_t));
    if (!g_devices) return -1;

    for (int i = 0; i < g_count; i++) {
        g_devices[i].index           = i;
        g_devices[i].op              = OP_IDLE;
        g_devices[i].active          = 0;
        g_devices[i].retry_count     = 0;
        g_devices[i].reconnect_count = 0;
        pthread_mutex_init(&g_devices[i].mutex, NULL);
    }

#ifdef _WIN32
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &g_csbi);
    g_start_line = g_csbi.dwCursorPosition.Y;
#else
    g_start_line = 0;
#endif

    printf("\n");
    for (int i = 0; i < g_count; i++) {
        printf("[%d] %-6s  [", i, "------");
        for (int j = 0; j < PROGRESS_BAR_WIDTH; j++) printf(" ");
        printf("]   0%% %s\n", "IDLE     ");
    }

    return 0;
}

void progress_cleanup(void)
{
    if (g_devices) {
        for (int i = 0; i < g_count; i++) {
            pthread_mutex_destroy(&g_devices[i].mutex);
        }
        free(g_devices);
        g_devices = NULL;
    }
    g_count = 0;
}

void progress_set_device(int index, const char *sn)
{
    if (index < 0 || index >= g_count) return;
    pthread_mutex_lock(&g_devices[index].mutex);
    if (sn) strncpy(g_devices[index].sn, sn, sizeof(g_devices[index].sn) - 1);
    g_devices[index].active = 1;
    pthread_mutex_unlock(&g_devices[index].mutex);
    progress_render_line(index);
}

void progress_update(int index, operation_t op, uint32_t current, uint32_t total)
{
    if (index < 0 || index >= g_count) return;
    pthread_mutex_lock(&g_devices[index].mutex);
    g_devices[index].op      = op;
    g_devices[index].current = current;
    g_devices[index].total   = total;
    if (total > 0) {
        g_devices[index].percent = (int)((current * 100) / total);
        if (g_devices[index].percent > 100) g_devices[index].percent = 100;
    }
    pthread_mutex_unlock(&g_devices[index].mutex);
    progress_render_line(index);
}

void progress_set_retry(int index, int retry)
{
    if (index < 0 || index >= g_count) return;
    pthread_mutex_lock(&g_devices[index].mutex);
    g_devices[index].retry_count = retry;
    pthread_mutex_unlock(&g_devices[index].mutex);
    progress_render_line(index);
}

void progress_set_reconnect(int index, int reconnect_attempt)
{
    if (index < 0 || index >= g_count) return;
    pthread_mutex_lock(&g_devices[index].mutex);
    g_devices[index].reconnect_count = reconnect_attempt;
    g_devices[index].op = OP_RECONNECTING;
    pthread_mutex_unlock(&g_devices[index].mutex);
    progress_render_line(index);
}

void progress_set_disconnected(int index)
{
    if (index < 0 || index >= g_count) return;
    pthread_mutex_lock(&g_devices[index].mutex);
    g_devices[index].op = OP_DISCONNECTED;
    pthread_mutex_unlock(&g_devices[index].mutex);
    progress_render_line(index);
}

void progress_set_reconnecting(int index)
{
    if (index < 0 || index >= g_count) return;
    pthread_mutex_lock(&g_devices[index].mutex);
    g_devices[index].op = OP_RECONNECTING;
    pthread_mutex_unlock(&g_devices[index].mutex);
    progress_render_line(index);
}

void progress_set_failed(int index)
{
    if (index < 0 || index >= g_count) return;
    pthread_mutex_lock(&g_devices[index].mutex);
    g_devices[index].op = OP_FAILED;
    pthread_mutex_unlock(&g_devices[index].mutex);
    progress_render_line(index);
}

void progress_set_done(int index)
{
    if (index < 0 || index >= g_count) return;
    pthread_mutex_lock(&g_devices[index].mutex);
    g_devices[index].op = OP_DONE;
    g_devices[index].percent = 100;
    g_devices[index].current = g_devices[index].total;
    pthread_mutex_unlock(&g_devices[index].mutex);
    progress_render_line(index);
}

void progress_render_line(int index)
{
    if (index < 0 || index >= g_count) return;

    char sn[64];
    int  percent;
    operation_t op;
    int  retry;
    int  reconnect;

    pthread_mutex_lock(&g_devices[index].mutex);
    strncpy(sn, g_devices[index].sn, sizeof(sn) - 1);
    sn[sizeof(sn) - 1] = '\0';
    percent   = g_devices[index].percent;
    op        = g_devices[index].op;
    retry     = g_devices[index].retry_count;
    reconnect = g_devices[index].reconnect_count;
    pthread_mutex_unlock(&g_devices[index].mutex);

    if (sn[0] == '\0') strcpy(sn, "------");

#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    set_cursor_pos(0, g_start_line + 1 + index);
#else
    set_cursor_pos(0, g_start_line + 1 + index);
#endif

    printf("[%d] %-6s  [", index, sn);

    int filled = (percent * PROGRESS_BAR_WIDTH) / 100;

#ifdef _WIN32
    SetConsoleTextAttribute(hConsole, get_op_color(op));
#else
    printf("%s", get_op_color_ansi(op));
#endif

    for (int j = 0; j < filled; j++) printf("=");

#ifdef _WIN32
    SetConsoleTextAttribute(hConsole, 7);
#else
    printf("\033[0m");
#endif

    for (int j = filled; j < PROGRESS_BAR_WIDTH; j++) printf(" ");

    printf("] %3d%% %s", percent, operation_str(op));

    if (retry > 0) {
        printf(" R%d", retry);
    }
    if (reconnect > 0) {
        printf(" RC%d", reconnect);
    }

    printf("       ");
}

void progress_render(void)
{
    for (int i = 0; i < g_count; i++) {
        progress_render_line(i);
    }
}
