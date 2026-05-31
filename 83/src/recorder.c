#include "recorder.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <png.h>

extern AuditLogger g_logger;

bool recorder_init(SessionRecorder *rec, const char *output_dir, const char *session_id, int interval)
{
    memset(rec, 0, sizeof(*rec));
    strncpy(rec->output_dir, output_dir, sizeof(rec->output_dir) - 1);
    strncpy(rec->session_id, session_id, sizeof(rec->session_id) - 1);
    rec->screenshot_interval = interval;
    rec->last_screenshot_time = 0;
    rec->frame_count = 0;
    rec->initialized = true;
    return true;
}

static bool write_png(const char *filepath, const uint8_t *framebuffer, int width, int height, int depth)
{
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to create PNG file: %s\n", filepath);
        return false;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }

    png_init_io(png_ptr, fp);

    int bpp = depth / 8;
    if (bpp < 3) bpp = 3;
    int color_type = (bpp >= 4) ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
    int bit_depth = 8;

    png_set_IHDR(png_ptr, info_ptr, width, height, bit_depth, color_type,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
    if (!row_pointers) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }

    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_bytep)(framebuffer + y * width * bpp);
    }

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, NULL);

    free(row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return true;
}

bool recorder_screenshot(SessionRecorder *rec, const uint8_t *framebuffer, int width, int height, int depth)
{
    if (!rec->initialized || !framebuffer) return false;

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s_frame_%05d.png",
             rec->output_dir, rec->session_id, rec->frame_count);

    bool ok = write_png(filepath, framebuffer, width, height, depth);
    if (ok) {
        rec->frame_count++;
        logger_log_screenshot(&g_logger, filepath);
    }
    return ok;
}

void recorder_maybe_screenshot(SessionRecorder *rec, const uint8_t *framebuffer, int width, int height, int depth)
{
    if (!rec->initialized) return;

    int now = (int)time(NULL);
    if (now - rec->last_screenshot_time < rec->screenshot_interval) return;

    if (recorder_screenshot(rec, framebuffer, width, height, depth)) {
        rec->last_screenshot_time = now;
    }
}

void recorder_close(SessionRecorder *rec)
{
    rec->initialized = false;
}
