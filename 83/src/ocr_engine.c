#include "ocr_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <png.h>

static bool save_png_to_path(const char *filepath, const uint8_t *framebuffer,
                             int width, int height, int depth,
                             int roi_x, int roi_y, int roi_w, int roi_h)
{
    if (roi_x < 0) roi_x = 0;
    if (roi_y < 0) roi_y = 0;
    if (roi_x + roi_w > width) roi_w = width - roi_x;
    if (roi_y + roi_h > height) roi_h = height - roi_y;

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        fprintf(stderr, "[OCR] Failed to create evidence file: %s\n", filepath);
        return false;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) { fclose(fp); return false; }

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

    png_set_IHDR(png_ptr, info_ptr, roi_w, roi_h, 8, color_type,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * roi_h);
    if (!row_pointers) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }

    for (int y = 0; y < roi_h; y++) {
        row_pointers[y] = (png_bytep)(framebuffer + ((roi_y + y) * width + roi_x) * bpp);
    }

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, NULL);

    free(row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return true;
}

bool ocr_engine_save_evidence_png(const char *dir,
                                  const uint8_t *framebuffer, int width, int height, int depth,
                                  const char *session_id, char *out_path, size_t out_path_len)
{
    mkdir(dir, 0700);

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", tm_info);

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s_evidence_%s.png",
             dir, session_id ? session_id : "unknown", time_str);

    if (!save_png_to_path(filepath, framebuffer, width, height, depth, 0, 0, width, height))
        return false;

    if (out_path && out_path_len > 0) {
        strncpy(out_path, filepath, out_path_len - 1);
        out_path[out_path_len - 1] = '\0';
    }
    return true;
}

static void *ocr_worker_thread(void *arg)
{
    OcrEngine *engine = (OcrEngine *)arg;

    while (engine->running) {
        pthread_mutex_lock(&engine->queue_mutex);

        while (engine->queue_count == 0 && engine->running) {
            pthread_cond_wait(&engine->queue_cond, &engine->queue_mutex);
        }

        if (!engine->running) {
            pthread_mutex_unlock(&engine->queue_mutex);
            break;
        }

        OcrTask *task = engine->task_queue[engine->queue_head];
        engine->queue_head = (engine->queue_head + 1) % engine->queue_capacity;
        engine->queue_count--;

        pthread_mutex_unlock(&engine->queue_mutex);

        if (!task) continue;

        int bpp = task->depth / 8;
        if (bpp < 3) bpp = 3;

        int w = task->region.enabled ? task->region.width : task->width;
        int h = task->region.enabled ? task->region.height : task->height;
        int x = task->region.enabled ? task->region.x : 0;
        int y = task->region.enabled ? task->region.y : 0;

        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + w > task->width) w = task->width - x;
        if (y + h > task->height) h = task->height - y;

        OcrResult result;
        memset(&result, 0, sizeof(result));
        result.hit = false;

        if (w > 0 && h > 0) {
            uint32_t *rgb_data = (uint32_t *)malloc(w * h * 4);
            if (rgb_data) {
                for (int row = 0; row < h; row++) {
                    for (int col = 0; col < w; col++) {
                        int src_offset = ((y + row) * task->width + (x + col)) * bpp;
                        uint8_t r = task->framebuffer[src_offset];
                        uint8_t g = task->framebuffer[src_offset + 1];
                        uint8_t b = task->framebuffer[src_offset + 2];
                        rgb_data[row * w + col] = (0xFF << 24) | (r << 16) | (g << 8) | b;
                    }
                }

                TessBaseAPI_SetImage(engine->tess, (const unsigned char *)rgb_data,
                                     w, h, 4, w * 4);

                char *text = TessBaseAPI_GetUTF8Text(engine->tess);
                if (text) {
                    strncpy(result.text, text, OCR_RESULT_MAX_LEN - 1);
                    TessBaseAPI_ClearText(engine->tess);
                    free(text);
                }

                free(rgb_data);
            }

            if (result.text[0] && engine->sensitive_words) {
                result.hit = sensitive_words_match(engine->sensitive_words, result.text,
                                                    result.matched_word, sizeof(result.matched_word));
            }

            if (result.hit) {
                time_t now = time(NULL);
                struct tm *tm_info = localtime(&now);
                strftime(result.timestamp, sizeof(result.timestamp),
                         "%Y-%m-%d %H:%M:%S", tm_info);

                ocr_engine_save_evidence_png(OCR_EVIDENCE_DIR,
                                            task->framebuffer, task->width, task->height, task->depth,
                                            task->session_id, result.evidence_path, sizeof(result.evidence_path));

                fprintf(stderr, "[OCR] SENSITIVE CONTENT DETECTED: \"%s\" | User: %s | IP: %s\n",
                        result.matched_word, task->username, task->client_ip);
                fprintf(stderr, "[OCR] Evidence saved: %s\n", result.evidence_path);

                if (engine->email_config && engine->email_config->initialized) {
                    char subject[256];
                    char body[EMAIL_ALERT_BODY_MAX];

                    snprintf(subject, sizeof(subject),
                             "[ALERT] VNC敏感内容检测: %s", result.matched_word);

                    snprintf(body, sizeof(body),
                             "VNC Watermark Audit Proxy - 敏感内容告警\r\n"
                             "\r\n"
                             "时间: %s\r\n"
                             "用户: %s\r\n"
                             "客户端IP: %s\r\n"
                             "会话ID: %s\r\n"
                             "命中敏感词: %s\r\n"
                             "证据截图: %s\r\n"
                             "\r\n"
                             "OCR识别文本(部分):\r\n"
                             "%.500s\r\n",
                             result.timestamp,
                             task->username,
                             task->client_ip,
                             task->session_id,
                             result.matched_word,
                             result.evidence_path,
                             result.text);

                    email_alert_send(engine->email_config, subject, body, result.evidence_path);
                }

                if (engine->on_hit_callback) {
                    engine->on_hit_callback(&result, engine->callback_user_data);
                }
            }
        }

        free(task->framebuffer);
        free(task);
    }

    return NULL;
}

bool ocr_engine_init(OcrEngine *engine,
                     const char *lang,
                     SensitiveWords *sw,
                     EmailAlertConfig *email_cfg,
                     int scan_interval)
{
    memset(engine, 0, sizeof(*engine));

    engine->tess = TessBaseAPICreate();
    if (!engine->tess) {
        fprintf(stderr, "[OCR] Failed to create Tesseract instance\n");
        return false;
    }

    const char *use_lang = lang ? lang : "chi_sim+eng";
    strncpy(engine->ocr_lang, use_lang, sizeof(engine->ocr_lang) - 1);

    if (TessBaseAPIInit3(engine->tess, NULL, use_lang) != 0) {
        fprintf(stderr, "[OCR] Failed to initialize Tesseract with language: %s\n", use_lang);
        fprintf(stderr, "[OCR] Falling back to english only\n");
        if (TessBaseAPIInit3(engine->tess, NULL, "eng") != 0) {
            fprintf(stderr, "[OCR] Failed to initialize Tesseract\n");
            TessBaseAPIDelete(engine->tess);
            engine->tess = NULL;
            return false;
        }
    }

    TessBaseAPISetPageSegMode(engine->tess, PSM_AUTO);

    engine->sensitive_words = sw;
    engine->email_config = email_cfg;
    engine->scan_interval = scan_interval > 0 ? scan_interval : OCR_SCAN_INTERVAL;

    engine->queue_capacity = 16;
    engine->task_queue = (OcrTask **)calloc(engine->queue_capacity, sizeof(OcrTask *));
    if (!engine->task_queue) {
        TessBaseAPIDelete(engine->tess);
        return false;
    }

    pthread_mutex_init(&engine->queue_mutex, NULL);
    pthread_cond_init(&engine->queue_cond, NULL);

    engine->running = 1;
    for (int i = 0; i < OCR_THREAD_POOL_SIZE; i++) {
        if (pthread_create(&engine->thread_pool[i], NULL, ocr_worker_thread, engine) != 0) {
            engine->running = 0;
            pthread_cond_broadcast(&engine->queue_cond);
            for (int j = 0; j < i; j++) pthread_join(engine->thread_pool[j], NULL);
            free(engine->task_queue);
            TessBaseAPIDelete(engine->tess);
            return false;
        }
    }

    engine->initialized = true;

    fprintf(stderr, "[OCR] Engine initialized (lang=%s, threads=%d, interval=%ds)\n",
            engine->ocr_lang, OCR_THREAD_POOL_SIZE, engine->scan_interval);

    return true;
}

void ocr_engine_cleanup(OcrEngine *engine)
{
    if (!engine->initialized) return;

    engine->running = 0;
    pthread_mutex_lock(&engine->queue_mutex);
    pthread_cond_broadcast(&engine->queue_cond);
    pthread_mutex_unlock(&engine->queue_mutex);

    for (int i = 0; i < OCR_THREAD_POOL_SIZE; i++) {
        pthread_join(engine->thread_pool[i], NULL);
    }

    pthread_mutex_lock(&engine->queue_mutex);
    while (engine->queue_count > 0) {
        OcrTask *task = engine->task_queue[engine->queue_head];
        engine->queue_head = (engine->queue_head + 1) % engine->queue_capacity;
        engine->queue_count--;
        if (task) {
            free(task->framebuffer);
            free(task);
        }
    }
    free(engine->task_queue);
    engine->task_queue = NULL;
    pthread_mutex_unlock(&engine->queue_mutex);

    pthread_mutex_destroy(&engine->queue_mutex);
    pthread_cond_destroy(&engine->queue_cond);

    if (engine->tess) {
        TessBaseAPIEnd(engine->tess);
        TessBaseAPIDelete(engine->tess);
        engine->tess = NULL;
    }

    engine->initialized = false;
    fprintf(stderr, "[OCR] Engine cleaned up\n");
}

bool ocr_engine_set_region(OcrEngine *engine, OcrRegion region)
{
    if (!engine->initialized) return false;
    engine->region = region;
    fprintf(stderr, "[OCR] Scan region set: x=%d, y=%d, w=%d, h=%d (%s)\n",
            region.x, region.y, region.width, region.height,
            region.enabled ? "enabled" : "full screen");
    return true;
}

bool ocr_engine_queue_task(OcrEngine *engine,
                           const uint8_t *framebuffer, int width, int height, int depth,
                           const char *session_id, const char *client_ip, const char *username)
{
    if (!engine->initialized || !framebuffer) return false;

    pthread_mutex_lock(&engine->queue_mutex);

    if (engine->queue_count >= engine->queue_capacity) {
        pthread_mutex_unlock(&engine->queue_mutex);
        return false;
    }

    OcrTask *task = (OcrTask *)calloc(1, sizeof(OcrTask));
    if (!task) {
        pthread_mutex_unlock(&engine->queue_mutex);
        return false;
    }

    int bpp = depth / 8;
    if (bpp < 3) bpp = 3;
    size_t fb_size = (size_t)width * height * bpp;

    task->framebuffer = (uint8_t *)malloc(fb_size);
    if (!task->framebuffer) {
        free(task);
        pthread_mutex_unlock(&engine->queue_mutex);
        return false;
    }

    memcpy(task->framebuffer, framebuffer, fb_size);
    task->width = width;
    task->height = height;
    task->depth = depth;
    task->region = engine->region;

    if (session_id) strncpy(task->session_id, session_id, sizeof(task->session_id) - 1);
    if (client_ip) strncpy(task->client_ip, client_ip, sizeof(task->client_ip) - 1);
    if (username) strncpy(task->username, username, sizeof(task->username) - 1);

    task->engine = engine;

    engine->task_queue[engine->queue_tail] = task;
    engine->queue_tail = (engine->queue_tail + 1) % engine->queue_capacity;
    engine->queue_count++;

    pthread_cond_signal(&engine->queue_cond);
    pthread_mutex_unlock(&engine->queue_mutex);

    return true;
}

void ocr_engine_hot_reload_words(OcrEngine *engine)
{
    if (!engine->initialized || !engine->sensitive_words) return;
    sensitive_words_hot_reload(engine->sensitive_words);
}
