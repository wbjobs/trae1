#ifndef WATERMARK_H
#define WATERMARK_H

#include <stdint.h>
#include <stdbool.h>

#define WATERMARK_FONT_WIDTH  5
#define WATERMARK_FONT_HEIGHT 7
#define WATERMARK_ALPHA       51
#define WATERMARK_MOVE_INTERVAL 5

typedef struct {
    int x;
    int y;
    char text[256];
    bool initialized;
    int last_move_time;
    int screen_width;
    int screen_height;
} WatermarkState;

void watermark_init(WatermarkState *wm, int screen_width, int screen_height);
void watermark_update_text(WatermarkState *wm, const char *client_ip, const char *username);
void watermark_render(WatermarkState *wm, uint8_t *framebuffer, int width, int height, int depth);
void watermark_maybe_move(WatermarkState *wm);

#endif
