#include "email_alert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <curl/curl.h>

struct upload_info {
    const char *data;
    size_t bytes_read;
    size_t total_size;
};

static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp)
{
    struct upload_info *upload = (struct upload_info *)userp;
    if (size * nmemb < 1 || upload->bytes_read >= upload->total_size)
        return 0;

    size_t available = upload->total_size - upload->bytes_read;
    size_t max = size * nmemb;
    size_t copy_len = (available < max) ? available : max;

    memcpy(ptr, upload->data + upload->bytes_read, copy_len);
    upload->bytes_read += copy_len;
    return copy_len;
}

bool email_alert_init(EmailAlertConfig *cfg,
                      const char *smtp_host, int smtp_port,
                      const char *smtp_user, const char *smtp_password,
                      const char *sender, const char *recipients[], int recipient_count,
                      bool use_tls)
{
    memset(cfg, 0, sizeof(*cfg));

    if (!smtp_host || !smtp_host[0]) {
        cfg->initialized = false;
        return false;
    }

    strncpy(cfg->smtp_host, smtp_host, sizeof(cfg->smtp_host) - 1);
    cfg->smtp_port = smtp_port > 0 ? smtp_port : EMAIL_ALERT_DEFAULT_SMTP_PORT;

    if (smtp_user) strncpy(cfg->smtp_user, smtp_user, sizeof(cfg->smtp_user) - 1);
    if (smtp_password) strncpy(cfg->smtp_password, smtp_password, sizeof(cfg->smtp_password) - 1);
    if (sender) strncpy(cfg->sender, sender, sizeof(cfg->sender) - 1);

    cfg->recipient_count = 0;
    if (recipients && recipient_count > 0) {
        for (int i = 0; i < recipient_count && i < EMAIL_ALERT_MAX_RECIPIENTS; i++) {
            if (recipients[i] && recipients[i][0]) {
                strncpy(cfg->recipients[cfg->recipient_count], recipients[i],
                        sizeof(cfg->recipients[cfg->recipient_count]) - 1);
                cfg->recipient_count++;
            }
        }
    }

    cfg->use_tls = use_tls;
    cfg->initialized = (cfg->recipient_count > 0);

    curl_global_init(CURL_GLOBAL_ALL);

    if (cfg->initialized) {
        fprintf(stderr, "[EmailAlert] Initialized: smtp://%s:%d (%d recipients)\n",
                cfg->smtp_host, cfg->smtp_port, cfg->recipient_count);
    } else {
        fprintf(stderr, "[EmailAlert] Disabled (no recipients or SMTP host)\n");
    }

    return cfg->initialized;
}

void email_alert_cleanup(EmailAlertConfig *cfg)
{
    if (cfg->initialized) {
        curl_global_cleanup();
    }
    cfg->initialized = false;
}

bool email_alert_send_blocking(EmailAlertConfig *cfg,
                               const char *subject, const char *body,
                               const char *screenshot_path)
{
    if (!cfg->initialized) return false;

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char url[512];
    snprintf(url, sizeof(url), "smtp%s://%s:%d",
             cfg->use_tls ? "s" : "", cfg->smtp_host, cfg->smtp_port);

    char message[EMAIL_ALERT_BODY_MAX + 1024];
    char date_header[128];

    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    strftime(date_header, sizeof(date_header),
             "%a, %d %b %Y %H:%M:%S +0000", tm_info);

    char to_header[1024] = "";
    for (int i = 0; i < cfg->recipient_count; i++) {
        if (i > 0) strncat(to_header, ", ", sizeof(to_header) - strlen(to_header) - 1);
        strncat(to_header, cfg->recipients[i], sizeof(to_header) - strlen(to_header) - 1);
    }

    char sender_addr[512];
    if (cfg->sender[0])
        snprintf(sender_addr, sizeof(sender_addr), "<%s>", cfg->sender);
    else if (cfg->smtp_user[0])
        snprintf(sender_addr, sizeof(sender_addr), "<%s>", cfg->smtp_user);
    else
        strncpy(sender_addr, "<vnc-proxy@localhost>", sizeof(sender_addr) - 1);

    int msg_len = snprintf(message, sizeof(message),
        "Date: %s\r\n"
        "To: %s\r\n"
        "From: VNC Watermark Proxy %s\r\n"
        "Subject: %s\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: text/plain; charset=UTF-8\r\n"
        "Content-Transfer-Encoding: 8bit\r\n"
        "\r\n"
        "%s\r\n"
        "\r\n"
        "-- \r\n"
        "Sent by VNC Watermark Audit Proxy\r\n",
        date_header, to_header, sender_addr,
        subject ? subject : "VNC Alert",
        body ? body : "");

    struct upload_info upload = { message, 0, (size_t)msg_len };

    struct curl_slist *recipients_list = NULL;
    for (int i = 0; i < cfg->recipient_count; i++) {
        recipients_list = curl_slist_append(recipients_list, cfg->recipients[i]);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, cfg->sender[0] ? cfg->sender : cfg->smtp_user);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients_list);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

    if (cfg->smtp_user[0] && cfg->smtp_password[0]) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, cfg->smtp_user);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg->smtp_password);
    }

    if (cfg->use_tls) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(recipients_list);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "[EmailAlert] Failed to send: %s\n", curl_easy_strerror(res));
        return false;
    }

    fprintf(stderr, "[EmailAlert] Alert sent to %d recipient(s): %s\n", cfg->recipient_count, subject);
    return true;
}

typedef struct {
    EmailAlertConfig cfg;
    char subject[EMAIL_ALERT_SUBJECT_MAX];
    char body[EMAIL_ALERT_BODY_MAX];
    char screenshot_path[512];
} EmailAlertTask;

static void *email_alert_thread_func(void *arg)
{
    EmailAlertTask *task = (EmailAlertTask *)arg;
    email_alert_send_blocking(&task->cfg, task->subject, task->body, task->screenshot_path);
    free(task);
    return NULL;
}

bool email_alert_send(EmailAlertConfig *cfg,
                      const char *subject, const char *body,
                      const char *screenshot_path)
{
    if (!cfg->initialized) return false;

    EmailAlertTask *task = (EmailAlertTask *)calloc(1, sizeof(EmailAlertTask));
    if (!task) return false;

    task->cfg = *cfg;
    if (subject) strncpy(task->subject, subject, sizeof(task->subject) - 1);
    if (body) strncpy(task->body, body, sizeof(task->body) - 1);
    if (screenshot_path) strncpy(task->screenshot_path, screenshot_path, sizeof(task->screenshot_path) - 1);

    pthread_t tid;
    if (pthread_create(&tid, NULL, email_alert_thread_func, task) != 0) {
        free(task);
        return false;
    }
    pthread_detach(tid);
    return true;
}
