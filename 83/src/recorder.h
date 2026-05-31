#ifndef RECORDER_H
#define RECORDER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char output_dir[512];
    char session_id[64];
    int screenshot_interval;
    int last_screenshot_time;
    int frame_count;
    bool initialized;
} SessionRecorder;

bool recorder_init(SessionRecorder *rec, const char *output_dir, const char *session_id, int interval);
bool recorder_screenshot(SessionRecorder *rec, const uint8_t *framebuffer, int width, int height, int depth);
void recorder_maybe_screenshot(SessionRecorder *rec, const uint8_t *framebuffer, int width, int height, int depth);
void recorder_close(SessionRecorder *rec);

#endif
