#ifndef TLS_H
#define TLS_H

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define TLS_CERT_DIR      "data/certs"
#define TLS_CERT_FILE     "data/certs/vnc_proxy.crt"
#define TLS_KEY_FILE      "data/certs/vnc_proxy.key"
#define TLS_TICKET_KEY_FILE "data/certs/ticket.key"
#define TLS_DEFAULT_VALIDITY_DAYS 365
#define TLS_RENEW_THRESHOLD_DAYS 30
#define TLS_SESSION_TICKET_LIFETIME 86400

typedef struct {
    SSL_CTX *server_ctx;
    SSL_CTX *client_ctx;
    X509 *cert;
    EVP_PKEY *pkey;
    char cert_path[512];
    char key_path[512];
    char ticket_key_path[512];
    int validity_days;
    bool auto_renew;
    bool initialized;
    bool use_tls;
    pthread_mutex_t mutex;
    uint8_t ticket_key[48];
    bool ticket_key_loaded;
} TlsContext;

bool tls_init(TlsContext *tls, bool use_tls, int validity_days, bool auto_renew);
void tls_cleanup(TlsContext *tls);

bool tls_generate_self_signed_cert(TlsContext *tls, const char *common_name);
bool tls_load_or_create_cert(TlsContext *tls, const char *cert_path, const char *key_path);
bool tls_save_cert(TlsContext *tls, const char *cert_path, const char *key_path);

bool tls_hot_reload(TlsContext *tls);
bool tls_check_and_renew(TlsContext *tls);

bool tls_setup_server_ctx(TlsContext *tls);
bool tls_setup_client_ctx(TlsContext *tls);

bool tls_setup_session_tickets(TlsContext *tls);
bool tls_save_ticket_key(TlsContext *tls, const char *path);
bool tls_load_ticket_key(TlsContext *tls, const char *path);

int tls_get_cert_days_remaining(TlsContext *tls);

X509 *tls_load_cert_from_file(const char *path);
EVP_PKEY *tls_load_key_from_file(const char *path);

const char *tls_get_last_error(void);

#endif
