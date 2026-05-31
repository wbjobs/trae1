#ifndef SANDBOX_H
#define SANDBOX_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

#define SANDBOX_TIMEOUT 5
#define SANDBOX_CPU_CORES 1
#define SANDBOX_MEMORY_MB 256
#define SANDBOX_MAX_OUTPUT 65536

typedef enum {
    SANDBOX_RESULT_UNKNOWN = 0,
    SANDBOX_RESULT_SAFE = 1,
    SANDBOX_RESULT_SUSPICIOUS = 2,
    SANDBOX_RESULT_DANGEROUS = 3,
    SANDBOX_RESULT_ERROR = 4,
    SANDBOX_RESULT_TIMEOUT = 5
} sandbox_result_t;

typedef struct {
    sandbox_result_t result;
    int risk_score;
    char detected_patterns[1024];
    char output[SANDBOX_MAX_OUTPUT];
    char error_msg[256];
    time_t execute_time;
    uint32_t execution_ms;
} sandbox_report_t;

typedef struct {
    int enabled;
    char mysql_host[64];
    int mysql_port;
    char mysql_user[64];
    char mysql_pass[128];
    char mysql_db[64];
    char chroot_path[512];
    char seccomp_profile[512];
    pthread_mutex_t mutex;
} sandbox_ctx_t;

sandbox_ctx_t *sandbox_create(void);
void sandbox_destroy(sandbox_ctx_t *ctx);
int sandbox_init(sandbox_ctx_t *ctx, const char *host, int port, 
                 const char *user, const char *pass, const char *db);
sandbox_report_t sandbox_execute(sandbox_ctx_t *ctx, const char *sql);
int sandbox_analyze_result(sandbox_report_t *report, const char *sql);
int sandbox_setup_cgroups(void);
int sandbox_setup_seccomp(void);

#endif
