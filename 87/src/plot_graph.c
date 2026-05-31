#include "sctp_transfer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define COLOR_RESET  "\x1b[0m"
#define COLOR_RED    "\x1b[31m"
#define COLOR_GREEN  "\x1b[32m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_BLUE   "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN   "\x1b[36m"

static const char *colors[] = {
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_CYAN,
    COLOR_MAGENTA,
    COLOR_RED
};

void plot_graph_init(plot_graph_t *pg, bool enabled)
{
    if (!pg) return;
    memset(pg, 0, sizeof(*pg));
    pg->enabled = enabled;
    pg->num_series = 0;
    pthread_mutex_init(&pg->lock, NULL);
}

void plot_graph_add_series(plot_graph_t *pg, const char *label, uint32_t color)
{
    if (!pg || !pg->enabled || !label) return;
    if (pg->num_series >= MAX_PATHS + 1) return;

    pthread_mutex_lock(&pg->lock);

    plot_series_t *s = &pg->series[pg->num_series];
    memset(s, 0, sizeof(*s));
    strncpy(s->label, label, sizeof(s->label) - 1);
    s->color = color;
    s->head = 0;
    s->count = 0;
    pg->num_series++;

    pthread_mutex_unlock(&pg->lock);
}

void plot_graph_add_data(plot_graph_t *pg, int series_idx, double value)
{
    if (!pg || !pg->enabled || series_idx < 0 || series_idx >= pg->num_series)
        return;

    pthread_mutex_lock(&pg->lock);

    plot_series_t *s = &pg->series[series_idx];
    s->data[s->head] = value;
    s->head = (s->head + 1) % PLOT_SAMPLES;
    if (s->count < PLOT_SAMPLES)
        s->count++;

    pthread_mutex_unlock(&pg->lock);
}

static double get_series_min(plot_series_t *s)
{
    double min_val = 1e18;
    int idx = (s->head - s->count + PLOT_SAMPLES) % PLOT_SAMPLES;
    for (int i = 0; i < s->count; i++) {
        if (s->data[idx] < min_val)
            min_val = s->data[idx];
        idx = (idx + 1) % PLOT_SAMPLES;
    }
    return min_val;
}

static double get_series_max(plot_series_t *s)
{
    double max_val = -1e18;
    int idx = (s->head - s->count + PLOT_SAMPLES) % PLOT_SAMPLES;
    for (int i = 0; i < s->count; i++) {
        if (s->data[idx] > max_val)
            max_val = s->data[idx];
        idx = (idx + 1) % PLOT_SAMPLES;
    }
    return max_val;
}

void plot_graph_render(plot_graph_t *pg)
{
    if (!pg || !pg->enabled || pg->num_series == 0) return;

    pthread_mutex_lock(&pg->lock);

    double global_min = 1e18;
    double global_max = -1e18;
    for (int s = 0; s < pg->num_series; s++) {
        if (pg->series[s].count == 0) continue;
        double min_v = get_series_min(&pg->series[s]);
        double max_v = get_series_max(&pg->series[s]);
        if (min_v < global_min) global_min = min_v;
        if (max_v > global_max) global_max = max_v;
    }

    if (global_min == global_max) {
        global_min -= 1.0;
        global_max += 1.0;
    }

    double range = global_max - global_min;
    if (range < 0.001) range = 1.0;

    char grid[PLOT_HEIGHT][PLOT_WIDTH + 1];
    for (int y = 0; y < PLOT_HEIGHT; y++) {
        memset(grid[y], ' ', PLOT_WIDTH);
        grid[y][PLOT_WIDTH] = '\0';
    }

    for (int s = 0; s < pg->num_series; s++) {
        plot_series_t *ser = &pg->series[s];
        if (ser->count == 0) continue;

        int start_idx = (ser->head - ser->count + PLOT_SAMPLES) % PLOT_SAMPLES;
        int x_offset = PLOT_WIDTH - ser->count;
        if (x_offset < 0) x_offset = 0;

        double prev_y = 0;
        int prev_x = -1;

        for (int i = 0; i < ser->count; i++) {
            int idx = (start_idx + i) % PLOT_SAMPLES;
            int x = x_offset + i;
            if (x >= PLOT_WIDTH) break;

            double val = ser->data[idx];
            double norm_y = (val - global_min) / range;
            int y = PLOT_HEIGHT - 1 - (int)(norm_y * (PLOT_HEIGHT - 1));
            if (y < 0) y = 0;
            if (y >= PLOT_HEIGHT) y = PLOT_HEIGHT - 1;

            grid[y][x] = '0' + s;

            if (prev_x >= 0 && abs(y - (int)prev_y) > 1) {
                int y_start = y < (int)prev_y ? y : (int)prev_y;
                int y_end = y > (int)prev_y ? y : (int)prev_y;
                for (int yi = y_start + 1; yi < y_end; yi++) {
                    if (grid[yi][x] == ' ')
                        grid[yi][x] = '|';
                }
            }
            prev_y = y;
            prev_x = x;
        }
    }

    printf("\n");
    for (int y = 0; y < PLOT_HEIGHT; y++) {
        double val = global_max - (double)y / (PLOT_HEIGHT - 1) * range;
        printf("%8.1f |", val);

        for (int x = 0; x < PLOT_WIDTH; x++) {
            char c = grid[y][x];
            if (c >= '0' && c <= '9') {
                int s = c - '0';
                if (s < pg->num_series) {
                    const char *color = colors[s % 5];
                    printf("%s*%s", color, COLOR_RESET);
                } else {
                    printf("%c", c);
                }
            } else {
                printf("%c", c);
            }
        }
        printf("\n");
    }

    printf("%8s +", "");
    for (int x = 0; x < PLOT_WIDTH; x++)
        printf("-");
    printf("\n");

    printf("%8s   ", "");
    for (int s = 0; s < pg->num_series; s++) {
        const char *color = colors[s % 5];
        printf("  %s*%s %s", color, COLOR_RESET, pg->series[s].label);
    }
    printf("\n");

    pthread_mutex_unlock(&pg->lock);
}

void plot_graph_destroy(plot_graph_t *pg)
{
    if (!pg) return;
    pthread_mutex_destroy(&pg->lock);
    pg->enabled = false;
    pg->num_series = 0;
}
