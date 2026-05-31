#ifndef EMAIL_ALERT_H
#define EMAIL_ALERT_H

#include <stdbool.h>

#define EMAIL_ALERT_DEFAULT_SMTP_PORT 587
#define EMAIL_ALERT_MAX_RECIPIENTS 8
#define EMAIL_ALERT_BODY_MAX 4096
#define EMAIL_ALERT_SUBJECT_MAX 256

typedef struct {
    char smtp_host[256];
    int smtp_port;
    char smtp_user[128];
    char smtp_password[128];
    char sender[256];
    char recipients[EMAIL_ALERT_MAX_RECIPIENTS][256];
    int recipient_count;
    bool use_tls;
    bool initialized;
} EmailAlertConfig;

bool email_alert_init(EmailAlertConfig *cfg,
                      const char *smtp_host, int smtp_port,
                      const char *smtp_user, const char *smtp_password,
                      const char *sender, const char *recipients[], int recipient_count,
                      bool use_tls);
void email_alert_cleanup(EmailAlertConfig *cfg);

bool email_alert_send(EmailAlertConfig *cfg,
                      const char *subject, const char *body,
                      const char *screenshot_path);

bool email_alert_send_blocking(EmailAlertConfig *cfg,
                               const char *subject, const char *body,
                               const char *screenshot_path);

#endif
