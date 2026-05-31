#include "sctp_transfer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <termios.h>
#endif

static bool g_is_sender = false;
static time_t g_start_time;
static bool g_tty_enabled = false;

static int get_terminal_width(void)
{
#ifdef TIOCGWINSZ
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0)
        return ws.ws_col;
#endif
    return 80;
}

static void format_speed(char *buf, size_t buf_len, uint64_t speed_bps)
{
    if (speed_bps >= 1024 * 1024 * 1024) {
        snprintf(buf, buf_len, "%.2f GB/s",
                 (double)speed_bps / (1024 * 1024 * 1024));
    } else if (speed_bps >= 1024 * 1024) {
        snprintf(buf, buf_len, "%.2f MB/s",
                 (double)speed_bps / (1024 * 1024));
    } else if (speed_bps >= 1024) {
        snprintf(buf, buf_len, "%.2f KB/s",
                 (double)speed_bps / 1024);
    } else {
        snprintf(buf, buf_len, "%lu B/s", (unsigned long)speed_bps);
    }
}

static void format_bytes(char *buf, size_t buf_len, uint64_t bytes)
{
    if (bytes >= (uint64_t)1024 * 1024 * 1024 * 1024) {
        snprintf(buf, buf_len, "%.2f TB",
                 (double)bytes / (1024ULL * 1024 * 1024 * 1024));
    } else if (bytes >= (uint64_t)1024 * 1024 * 1024) {
        snprintf(buf, buf_len, "%.2f GB",
                 (double)bytes / (1024 * 1024 * 1024));
    } else if (bytes >= (uint64_t)1024 * 1024) {
        snprintf(buf, buf_len, "%.2f MB",
                 (double)bytes / (1024 * 1024));
    } else if (bytes >= 1024) {
        snprintf(buf, buf_len, "%.2f KB", (double)bytes / 1024);
    } else {
        snprintf(buf, buf_len, "%lu B", (unsigned long)bytes);
    }
}

void display_init(bool is_sender)
{
    g_is_sender = is_sender;
    g_start_time = time(NULL);
    g_tty_enabled = isatty(1);

    printf("\n=== SCTP Multi-Path File %s ===\n\n",
           is_sender ? "Sender" : "Receiver");
    printf("%-12s %16s %14s %10s %s\n",
           "Path", "Transferred", "Speed", "RTT", "Status");
    printf("%s\n", "-----------------------------------------------------------"
                     "-------------------");
}

static const char *state_str(path_state_t state)
{
    switch (state) {
        case PATH_STATE_HEALTHY:  return "HEALTHY";
        case PATH_STATE_SLOW:     return "SLOW";
        case PATH_STATE_DEGRADED: return "DEGRADED";
        case PATH_STATE_DOWN:     return "DOWN";
        default:                  return "UNKNOWN";
    }
}

void display_update(const transfer_stats_t *stats)
{
    if (g_tty_enabled)
        printf("\x1b[%dA\x1b[J", stats->num_total + 5);

    char speed_buf[32];
    char bytes_buf[32];

    for (int i = 0; i < stats->num_total; i++) {
        format_speed(speed_buf, sizeof(speed_buf), stats->paths[i].speed);
        format_bytes(bytes_buf, sizeof(bytes_buf), stats->paths[i].bytes);

        if (stats->paths[i].rtt_us > 0) {
            printf("%-12s %16s %14s %8.1f ms  %s\n",
                   stats->paths[i].name,
                   bytes_buf,
                   speed_buf,
                   (double)stats->paths[i].rtt_us / 1000.0,
                   state_str(stats->paths[i].state));
        } else {
            printf("%-12s %16s %14s %8s  %s\n",
                   stats->paths[i].name,
                   bytes_buf,
                   speed_buf,
                   "-",
                   state_str(stats->paths[i].state));
        }
    }

    printf("%s\n", "-----------------------------------------------------------"
                     "-------------------");

    format_speed(speed_buf, sizeof(speed_buf), stats->total_speed);
    format_bytes(bytes_buf, sizeof(bytes_buf),
                 (uint64_t)(stats->progress_pct / 100.0 *
                            (double)stats->total_bytes));

    printf("%-12s %16s %14s\n",
           "TOTAL", bytes_buf, speed_buf);

    int width = get_terminal_width() - 20;
    if (width < 10) width = 10;
    int filled = (int)((stats->progress_pct / 100.0) * width);

    printf("\nProgress: [");
    for (int i = 0; i < width; i++) {
        if (i < filled)
            printf("=");
        else
            printf(" ");
    }
    printf("] %5.1f%%", stats->progress_pct);

    if (stats->reorder_buffer_count > 0) {
        printf("\nReorder buffer: %u / %d entries",
               stats->reorder_buffer_count, REORDER_BUFFER_SIZE);
    }

    printf("\n");

    time_t elapsed = time(NULL) - g_start_time;
    if (elapsed > 0 && stats->total_speed > 0 && stats->total_bytes > 0) {
        uint64_t transferred = (uint64_t)(stats->progress_pct / 100.0 *
                                (double)stats->total_bytes);
        uint64_t remaining = (transferred < stats->total_bytes) ?
                             (stats->total_bytes - transferred) : 0;
        uint64_t eta_sec = (stats->total_speed > 0 && remaining > 0) ?
                           (remaining / stats->total_speed) : 0;

        printf("  Elapsed: %lds  Active: %d/%d  ETA: %lds\n",
               elapsed, stats->num_active, stats->num_total, eta_sec);
    } else {
        printf("  Elapsed: %lds  Active: %d/%d\n",
               elapsed, stats->num_active, stats->num_total);
    }
}

void display_final(const transfer_stats_t *stats, bool success)
{
    printf("\n%s\n", "==========================================================="
                     "===================");
    printf("Transfer %s\n\n", success ? "COMPLETED" : "FAILED");

    char speed_buf[32];
    char bytes_buf[32];

    for (int i = 0; i < stats->num_total; i++) {
        format_speed(speed_buf, sizeof(speed_buf), stats->paths[i].speed);
        format_bytes(bytes_buf, sizeof(bytes_buf), stats->paths[i].bytes);

        if (stats->paths[i].rtt_us > 0) {
            printf("  %-12s %16s  Avg: %14s  RTT: %.1f ms\n",
                   stats->paths[i].name,
                   bytes_buf,
                   speed_buf,
                   (double)stats->paths[i].rtt_us / 1000.0);
        } else {
            printf("  %-12s %16s  Avg: %14s\n",
                   stats->paths[i].name,
                   bytes_buf,
                   speed_buf);
        }
    }

    printf("\n");
    format_bytes(bytes_buf, sizeof(bytes_buf), stats->total_bytes);
    time_t total = time(NULL) - g_start_time;
    printf("  Total time: %lds  Total data: %s  Success: %s\n",
           total, bytes_buf, success ? "Yes" : "No");
    printf("%s\n", "==========================================================="
                     "===================");
}

void display_shutdown(void)
{
    printf("\n");
}
