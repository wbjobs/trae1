#ifndef OCR_ENGINE_H
#define OCR_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <tesseract/capi.h>
#include <leptonica/allheaders.h>
#include "sensitive_words.h"
#include "email_alert.h"

#define OCR_THREAD_POOL_SIZE 2
#define OCR_SCAN_INTERVAL 10
#define OCR_RESULT_MAX_LEN 8192
#define OCR_EVIDENCE_DIR "data/evidence"

typedef struct OcrRegion {
    int x;
    int y;
    int width;
    int height;
    bool enabled;
} OcrRegion;

typedef struct OcrTask {
    uint8_t *framebuffer;
    int width;
    int height;
    int depth;
    OcrRegion region;
    char session_id[64];
    char client_ip[64];
    char username[256];
    struct OcrEngine *engine;
} OcrTask;

typedef struct OcrResult {
    char text[OCR_RESULT_MAX_LEN];
    char matched_word[128];
    bool hit;
    char evidence_path[512];
    char timestamp[64];
} OcrResult;

typedef struct OcrEngine {
    TessBaseAPI *tess;
    pthread_t thread_pool[OCR_THREAD_POOL_SIZE];
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    OcrTask **task_queue;
    int queue_size;
    int queue_capacity;
    int queue_head;
    int queue_tail;
    int queue_count;
    int running;
    int scan_interval;
    OcrRegion region;
    char ocr_lang[16];
    SensitiveWords *sensitive_words;
    EmailAlertConfig *email_config;
    void (*on_hit_callback)(OcrResult *result, void *user_data);
    void *callback_user_data;
    bool initialized;
} OcrEngine;

bool ocr_engine_init(OcrEngine *engine,
                     const char *lang,
                     SensitiveWords *sw,
                     EmailAlertConfig *email_cfg,
                     int scan_interval);
void ocr_engine_cleanup(OcrEngine *engine);

bool ocr_engine_set_region(OcrEngine *engine, OcrRegion region);
bool ocr_engine_queue_task(OcrEngine *engine,
                           const uint8_t *framebuffer, int width, int height, int depth,
                           const char *session_id, const char *client_ip, const char *username);
void ocr_engine_hot_reload_words(OcrEngine *engine);

bool ocr_engine_save_evidence_png(const char *dir,
                                  const uint8_t *framebuffer, int width, int height, int depth,
                                  const char *session_id, char *out_path, size_t out_path_len);

#endif
