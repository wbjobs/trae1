#include "tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

static char g_tls_error[512];
static uint8_t g_ticket_key[48];
static bool g_ticket_key_ready = false;

const char *tls_get_last_error(void)
{
    return g_tls_error;
}

static void set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_tls_error, sizeof(g_tls_error), fmt, ap);
    va_end(ap);
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return (S_ISDIR(st.st_mode)) ? 0 : -1;
    }
    return mkdir(path, 0700);
}

static void ensure_default_dirs(void)
{
    ensure_dir("data");
    ensure_dir(TLS_CERT_DIR);
}

int tls_get_cert_days_remaining(TlsContext *tls)
{
    if (!tls->cert) return -1;
    const ASN1_TIME *not_after = X509_get0_notAfter(tls->cert);
    if (!not_after) return -1;

    time_t now = time(NULL);
    struct tm tm_not_after;
    memset(&tm_not_after, 0, sizeof(tm_not_after));

    const char *p = (const char *)not_after->data;
    if (not_after->type == V_ASN1_UTCTIME) {
        tm_not_after.tm_year = (p[0]-'0')*10 + (p[1]-'0');
        if (tm_not_after.tm_year < 50) tm_not_after.tm_year += 100;
    } else {
        tm_not_after.tm_year = (p[0]-'0')*1000 + (p[1]-'0')*100 + (p[2]-'0')*10 + (p[3]-'0') - 1900;
        p += 2;
    }
    tm_not_after.tm_mon  = (p[2]-'0')*10 + (p[3]-'0') - 1;
    tm_not_after.tm_mday = (p[4]-'0')*10 + (p[5]-'0');
    tm_not_after.tm_hour = (p[6]-'0')*10 + (p[7]-'0');
    tm_not_after.tm_min  = (p[8]-'0')*10 + (p[9]-'0');
    tm_not_after.tm_sec  = (p[10]-'0')*10 + (p[11]-'0');

    time_t expire = mktime(&tm_not_after);
    double diff = difftime(expire, now);
    return (int)(diff / 86400.0);
}

bool tls_generate_self_signed_cert(TlsContext *tls, const char *common_name)
{
    bool ret = false;
    EVP_PKEY *pkey = NULL;
    X509 *cert = NULL;
    RSA *rsa = NULL;
    BIGNUM *bn = NULL;
    X509_NAME *name = NULL;

    pkey = EVP_PKEY_new();
    if (!pkey) { set_error("EVP_PKEY_new failed"); goto cleanup; }

    rsa = RSA_new();
    if (!rsa) { set_error("RSA_new failed"); goto cleanup; }

    bn = BN_new();
    if (!bn) { set_error("BN_new failed"); goto cleanup; }
    if (!BN_set_word(bn, RSA_F4)) { set_error("BN_set_word failed"); goto cleanup; }
    if (!RSA_generate_key_ex(rsa, 2048, bn, NULL)) {
        set_error("RSA_generate_key_ex failed");
        goto cleanup;
    }
    if (!EVP_PKEY_assign_RSA(pkey, rsa)) { set_error("EVP_PKEY_assign_RSA failed"); goto cleanup; }
    rsa = NULL;

    cert = X509_new();
    if (!cert) { set_error("X509_new failed"); goto cleanup; }

    ASN1_INTEGER_set(X509_get_serialNumber(cert), (long)time(NULL));
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), (long)(tls->validity_days * 86400));
    X509_set_pubkey(cert, pkey);

    name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (const unsigned char *)(common_name ? common_name : "VNC-Watermark-Proxy"),
                               -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                               (const unsigned char *)"VNC Audit Proxy", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "OU", MBSTRING_ASC,
                               (const unsigned char *)"Security", -1, -1, 0);

    X509_set_issuer_name(cert, name);
    X509_sign(cert, pkey, EVP_sha256());

    if (tls->cert) X509_free(tls->cert);
    if (tls->pkey) EVP_PKEY_free(tls->pkey);
    tls->cert = cert;
    tls->pkey = pkey;
    ret = true;

cleanup:
    if (!ret) {
        if (cert) X509_free(cert);
        if (pkey) EVP_PKEY_free(pkey);
    }
    if (rsa) RSA_free(rsa);
    if (bn) BN_free(bn);
    return ret;
}

bool tls_save_cert(TlsContext *tls, const char *cert_path, const char *key_path)
{
    ensure_default_dirs();

    FILE *f = fopen(cert_path, "wb");
    if (!f) { set_error("Cannot write cert: %s", strerror(errno)); return false; }
    PEM_write_X509(f, tls->cert);
    fclose(f);
    chmod(cert_path, 0644);

    f = fopen(key_path, "wb");
    if (!f) { set_error("Cannot write key: %s", strerror(errno)); return false; }
    PEM_write_PrivateKey(f, tls->pkey, NULL, NULL, 0, NULL, NULL);
    fclose(f);
    chmod(key_path, 0600);

    strncpy(tls->cert_path, cert_path, sizeof(tls->cert_path) - 1);
    strncpy(tls->key_path, key_path, sizeof(tls->key_path) - 1);
    return true;
}

X509 *tls_load_cert_from_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { set_error("Cannot open cert: %s", strerror(errno)); return NULL; }
    X509 *cert = PEM_read_X509(f, NULL, NULL, NULL);
    fclose(f);
    return cert;
}

EVP_PKEY *tls_load_key_from_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { set_error("Cannot open key: %s", strerror(errno)); return NULL; }
    EVP_PKEY *pkey = PEM_read_PrivateKey(f, NULL, NULL, NULL);
    fclose(f);
    return pkey;
}

bool tls_load_or_create_cert(TlsContext *tls, const char *cert_path, const char *key_path)
{
    ensure_default_dirs();

    struct stat st;
    bool cert_exists = (stat(cert_path, &st) == 0);

    if (cert_exists) {
        X509 *c = tls_load_cert_from_file(cert_path);
        EVP_PKEY *k = tls_load_key_from_file(key_path);
        if (!c || !k) {
            fprintf(stderr, "[TLS] Failed to load existing cert/key, regenerating\n");
            if (c) X509_free(c);
            if (k) EVP_PKEY_free(k);
            cert_exists = false;
        } else {
            if (tls->cert) X509_free(tls->cert);
            if (tls->pkey) EVP_PKEY_free(tls->pkey);
            tls->cert = c;
            tls->pkey = k;
            strncpy(tls->cert_path, cert_path, sizeof(tls->cert_path) - 1);
            strncpy(tls->key_path, key_path, sizeof(tls->key_path) - 1);
            fprintf(stderr, "[TLS] Loaded existing certificate (CN-based)\n");
            return true;
        }
    }

    fprintf(stderr, "[TLS] Generating new self-signed certificate (validity: %d days)\n", tls->validity_days);
    if (!tls_generate_self_signed_cert(tls, "VNC-Watermark-Proxy")) return false;
    if (!tls_save_cert(tls, cert_path, key_path)) return false;
    return true;
}

static int ticket_key_callback(SSL *ssl, unsigned char *name, unsigned char *iv,
                               EVP_CIPHER_CTX *ectx, HMAC_CTX *hctx, int enc)
{
    (void)ssl;
    if (!g_ticket_key_ready) return 0;

    if (enc == 1) {
        memset(name, 0, 16);
        memcpy(name, "VNC-PROXY-TKT", 14);

        if (RAND_bytes(iv, EVP_MAX_IV_LENGTH) <= 0) return 0;

        EVP_EncryptInit_ex(ectx, EVP_aes_256_cbc(), NULL, g_ticket_key, iv);
        HMAC_Init_ex(hctx, g_ticket_key + 32, 16, EVP_sha256(), NULL);
        return 1;
    } else {
        if (memcmp(name, "VNC-PROXY-TKT", 14) != 0) return 0;

        EVP_DecryptInit_ex(ectx, EVP_aes_256_cbc(), NULL, g_ticket_key, iv);
        HMAC_Init_ex(hctx, g_ticket_key + 32, 16, EVP_sha256(), NULL);
        return 1;
    }
}

bool tls_setup_session_tickets(TlsContext *tls)
{
    if (!tls->ticket_key_loaded) {
        if (RAND_bytes(tls->ticket_key, sizeof(tls->ticket_key)) <= 0) {
            set_error("RAND_bytes for ticket key failed");
            return false;
        }
        tls->ticket_key_loaded = true;
    }
    memcpy(g_ticket_key, tls->ticket_key, sizeof(g_ticket_key));
    g_ticket_key_ready = true;

    SSL_CTX_set_tlsext_ticket_key_cb(tls->server_ctx, ticket_key_callback);
    SSL_CTX_set_timeout(tls->server_ctx, TLS_SESSION_TICKET_LIFETIME);
    fprintf(stderr, "[TLS] Session tickets enabled (lifetime: %ds)\n", TLS_SESSION_TICKET_LIFETIME);
    return true;
}

bool tls_save_ticket_key(TlsContext *tls, const char *path)
{
    ensure_default_dirs();
    FILE *f = fopen(path, "wb");
    if (!f) { set_error("Cannot write ticket key: %s", strerror(errno)); return false; }
    fwrite(tls->ticket_key, 1, sizeof(tls->ticket_key), f);
    fclose(f);
    chmod(path, 0600);
    strncpy(tls->ticket_key_path, path, sizeof(tls->ticket_key_path) - 1);
    return true;
}

bool tls_load_ticket_key(TlsContext *tls, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(tls->ticket_key, 1, sizeof(tls->ticket_key), f);
    fclose(f);
    if (n != sizeof(tls->ticket_key)) return false;
    tls->ticket_key_loaded = true;
    memcpy(g_ticket_key, tls->ticket_key, sizeof(g_ticket_key));
    g_ticket_key_ready = true;
    strncpy(tls->ticket_key_path, path, sizeof(tls->ticket_key_path) - 1);
    return true;
}

bool tls_setup_server_ctx(TlsContext *tls)
{
    tls->server_ctx = SSL_CTX_new(TLS_server_method());
    if (!tls->server_ctx) { set_error("SSL_CTX_new server failed"); return false; }

    SSL_CTX_set_min_proto_version(tls->server_ctx, TLS1_2_VERSION);

    if (tls->cert && tls->pkey) {
        if (!SSL_CTX_use_certificate(tls->server_ctx, tls->cert)) {
            set_error("SSL_CTX_use_certificate failed");
            return false;
        }
        if (!SSL_CTX_use_PrivateKey(tls->server_ctx, tls->pkey)) {
            set_error("SSL_CTX_use_PrivateKey failed");
            return false;
        }
        if (!SSL_CTX_check_private_key(tls->server_ctx)) {
            set_error("Private key check failed");
            return false;
        }
    }

    SSL_CTX_set_session_cache_mode(tls->server_ctx,
        SSL_SESS_CACHE_SERVER | SSL_SESS_CACHE_NO_INTERNAL);
    SSL_CTX_set_options(tls->server_ctx,
        SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
    SSL_CTX_set_cipher_list(tls->server_ctx, "HIGH:!aNULL:!MD5:!RC4");

    fprintf(stderr, "[TLS] Server TLS context created (TLS 1.2+)\n");
    return true;
}

bool tls_setup_client_ctx(TlsContext *tls)
{
    tls->client_ctx = SSL_CTX_new(TLS_client_method());
    if (!tls->client_ctx) { set_error("SSL_CTX_new client failed"); return false; }

    SSL_CTX_set_min_proto_version(tls->client_ctx, TLS1_2_VERSION);

    SSL_CTX_set_options(tls->client_ctx,
        SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);

    SSL_CTX_set_verify(tls->client_ctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_session_cache_mode(tls->client_ctx,
        SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL);

    fprintf(stderr, "[TLS] Client TLS context created (TLS 1.2+)\n");
    return true;
}

bool tls_check_and_renew(TlsContext *tls)
{
    if (!tls->initialized || !tls->auto_renew) return true;

    int days_left = tls_get_cert_days_remaining(tls);
    if (days_left < 0) {
        fprintf(stderr, "[TLS] Certificate validity check: unknown\n");
        return true;
    }

    fprintf(stderr, "[TLS] Certificate expires in %d days\n", days_left);

    if (days_left <= TLS_RENEW_THRESHOLD_DAYS) {
        fprintf(stderr, "[TLS] Certificate expires soon (≤%d days), renewing...\n", TLS_RENEW_THRESHOLD_DAYS);
        pthread_mutex_lock(&tls->mutex);

        if (!tls_generate_self_signed_cert(tls, "VNC-Watermark-Proxy")) {
            pthread_mutex_unlock(&tls->mutex);
            return false;
        }
        if (!tls_save_cert(tls, tls->cert_path, tls->key_path)) {
            pthread_mutex_unlock(&tls->mutex);
            return false;
        }

        if (tls->server_ctx) {
            SSL_CTX_use_certificate(tls->server_ctx, tls->cert);
            SSL_CTX_use_PrivateKey(tls->server_ctx, tls->pkey);
        }

        pthread_mutex_unlock(&tls->mutex);
        fprintf(stderr, "[TLS] Certificate renewed successfully\n");
    }

    return true;
}

bool tls_hot_reload(TlsContext *tls)
{
    if (!tls->initialized) return false;

    fprintf(stderr, "[TLS] Hot-reloading certificate...\n");
    pthread_mutex_lock(&tls->mutex);

    if (tls->cert_path[0] && tls->key_path[0]) {
        struct stat st_cert, st_key;
        if (stat(tls->cert_path, &st_cert) == 0 && stat(tls->key_path, &st_key) == 0) {
            X509 *new_cert = tls_load_cert_from_file(tls->cert_path);
            EVP_PKEY *new_key = tls_load_key_from_file(tls->key_path);
            if (new_cert && new_key) {
                if (tls->cert) X509_free(tls->cert);
                if (tls->pkey) EVP_PKEY_free(tls->pkey);
                tls->cert = new_cert;
                tls->pkey = new_key;

                if (tls->server_ctx) {
                    SSL_CTX_use_certificate(tls->server_ctx, tls->cert);
                    SSL_CTX_use_PrivateKey(tls->server_ctx, tls->pkey);
                }
                pthread_mutex_unlock(&tls->mutex);
                fprintf(stderr, "[TLS] Hot-reload completed: loaded %s\n", tls->cert_path);
                return true;
            }
            if (new_cert) X509_free(new_cert);
            if (new_key) EVP_PKEY_free(new_key);
        }
    }

    fprintf(stderr, "[TLS] Hot-reload: regenerating self-signed certificate\n");
    if (!tls_generate_self_signed_cert(tls, "VNC-Watermark-Proxy")) {
        pthread_mutex_unlock(&tls->mutex);
        return false;
    }
    tls_save_cert(tls, tls->cert_path[0] ? tls->cert_path : TLS_CERT_FILE,
                  tls->key_path[0] ? tls->key_path : TLS_KEY_FILE);

    if (tls->server_ctx) {
        SSL_CTX_use_certificate(tls->server_ctx, tls->cert);
        SSL_CTX_use_PrivateKey(tls->server_ctx, tls->pkey);
    }

    pthread_mutex_unlock(&tls->mutex);
    fprintf(stderr, "[TLS] Hot-reload completed: regenerated certificate\n");
    return true;
}

bool tls_init(TlsContext *tls, bool use_tls, int validity_days, bool auto_renew)
{
    memset(tls, 0, sizeof(*tls));
    tls->use_tls = use_tls;
    tls->validity_days = validity_days > 0 ? validity_days : TLS_DEFAULT_VALIDITY_DAYS;
    tls->auto_renew = auto_renew;
    pthread_mutex_init(&tls->mutex, NULL);

    if (!use_tls) {
        tls->initialized = true;
        return true;
    }

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    if (!tls_load_or_create_cert(tls, TLS_CERT_FILE, TLS_KEY_FILE)) return false;

    if (!tls_setup_server_ctx(tls)) return false;
    if (!tls_setup_client_ctx(tls)) return false;

    if (!tls_load_ticket_key(tls, TLS_TICKET_KEY_FILE)) {
        if (RAND_bytes(tls->ticket_key, sizeof(tls->ticket_key)) <= 0) {
            set_error("RAND_bytes failed");
            return false;
        }
        tls->ticket_key_loaded = true;
        tls_save_ticket_key(tls, TLS_TICKET_KEY_FILE);
    }

    if (!tls_setup_session_tickets(tls)) return false;

    tls_check_and_renew(tls);

    tls->initialized = true;
    return true;
}

void tls_cleanup(TlsContext *tls)
{
    if (!tls->initialized) return;

    pthread_mutex_lock(&tls->mutex);

    if (tls->server_ctx) {
        SSL_CTX_free(tls->server_ctx);
        tls->server_ctx = NULL;
    }
    if (tls->client_ctx) {
        SSL_CTX_free(tls->client_ctx);
        tls->client_ctx = NULL;
    }
    if (tls->cert) {
        X509_free(tls->cert);
        tls->cert = NULL;
    }
    if (tls->pkey) {
        EVP_PKEY_free(tls->pkey);
        tls->pkey = NULL;
    }

    pthread_mutex_unlock(&tls->mutex);
    pthread_mutex_destroy(&tls->mutex);
    tls->initialized = false;
    g_ticket_key_ready = false;

    EVP_cleanup();
    ERR_free_strings();
}
