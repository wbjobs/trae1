#include "sandbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <errno.h>

static const char *suspicious_patterns[] = {
    "information_schema",
    "mysql.user",
    "mysql.db",
    "@@version",
    "@@datadir",
    "@@hostname",
    "LOAD_FILE",
    "INTO OUTFILE",
    "INTO DUMPFILE",
    "xp_cmdshell",
    "sp_executesql",
    "EXEC(",
    "EXECUTE(",
    "system(",
    "sys_eval",
    "sys_exec",
    "lib_mysqludf_sys",
    "CREATE FUNCTION",
    "CREATE TABLE",
    "DROP TABLE",
    "DELETE FROM",
    "UPDATE mysql",
    "GRANT ALL",
    "INFORMATION_SCHEMA",
    "SCHEMATA",
    "TABLES",
    "COLUMNS",
    "USER_PRIVILEGES",
    "SCHEMA_PRIVILEGES",
    "TABLE_PRIVILEGES",
    "COLUMN_PRIVILEGES",
    "PROCESSLIST",
    "FILE_PRIV",
    "SUPER_PRIV",
    "root",
    "administrator",
    "SLEEP(",
    "BENCHMARK(",
    "WAITFOR",
    "DELAY",
    "master",
    "slave",
    "binlog",
    "relay_log",
    ".frm",
    ".MYD",
    ".MYI",
    "/etc/",
    "/var/",
    "/home/",
    "/root/",
    "/tmp/",
    "/proc/",
    "..\\",
    "../"
};

static const int pattern_count = sizeof(suspicious_patterns) / sizeof(suspicious_patterns[0]);

sandbox_ctx_t *sandbox_create(void) {
    sandbox_ctx_t *ctx = (sandbox_ctx_t *)calloc(1, sizeof(sandbox_ctx_t));
    if (!ctx) return NULL;

    ctx->enabled = 0;
    strcpy(ctx->mysql_host, "127.0.0.1");
    ctx->mysql_port = 3307;
    strcpy(ctx->mysql_user, "sandbox");
    strcpy(ctx->mysql_pass, "sandbox_pass");
    strcpy(ctx->mysql_db, "sandbox_db");
    pthread_mutex_init(&ctx->mutex, NULL);

    return ctx;
}

void sandbox_destroy(sandbox_ctx_t *ctx) {
    if (!ctx) return;
    pthread_mutex_destroy(&ctx->mutex);
    free(ctx);
}

int sandbox_init(sandbox_ctx_t *ctx, const char *host, int port,
                 const char *user, const char *pass, const char *db) {
    if (!ctx) return -1;

    if (host) strncpy(ctx->mysql_host, host, sizeof(ctx->mysql_host) - 1);
    if (port > 0) ctx->mysql_port = port;
    if (user) strncpy(ctx->mysql_user, user, sizeof(ctx->mysql_user) - 1);
    if (pass) strncpy(ctx->mysql_pass, pass, sizeof(ctx->mysql_pass) - 1);
    if (db) strncpy(ctx->mysql_db, db, sizeof(ctx->mysql_db) - 1);

    ctx->enabled = 1;

    sandbox_setup_cgroups();
    sandbox_setup_seccomp();

    return 0;
}

int sandbox_setup_cgroups(void) {
    return 0;
}

int sandbox_setup_seccomp(void) {
    return 0;
}

static void check_suspicious_patterns(sandbox_report_t *report, const char *sql) {
    if (!report || !sql) return;

    char sql_lower[65536];
    size_t sql_len = strlen(sql);
    if (sql_len >= sizeof(sql_lower)) {
        sql_len = sizeof(sql_lower) - 1;
    }
    for (size_t i = 0; i < sql_len; i++) {
        sql_lower[i] = sql[i] | 0x20;
    }
    sql_lower[sql_len] = '\0';

    char *detected = report->detected_patterns;
    detected[0] = '\0';
    int total_score = 0;

    for (int i = 0; i < pattern_count; i++) {
        char pattern_lower[256];
        size_t plen = strlen(suspicious_patterns[i]);
        for (size_t j = 0; j < plen; j++) {
            pattern_lower[j] = suspicious_patterns[i][j] | 0x20;
        }
        pattern_lower[plen] = '\0';

        if (strstr(sql_lower, pattern_lower)) {
            if (strlen(detected) + plen + 2 < sizeof(report->detected_patterns)) {
                if (detected[0]) strcat(detected, ",");
                strcat(detected, suspicious_patterns[i]);
            }

            if (i < 10) total_score += 30;
            else if (i < 30) total_score += 20;
            else total_score += 10;
        }
    }

    report->risk_score = total_score;

    if (total_score >= 60) {
        report->result = SANDBOX_RESULT_DANGEROUS;
    } else if (total_score >= 30) {
        report->result = SANDBOX_RESULT_SUSPICIOUS;
    } else {
        report->result = SANDBOX_RESULT_SAFE;
    }
}

static void analyze_sandbox_output(sandbox_report_t *report, const char *output) {
    if (!report || !output) return;

    if (strstr(output, "Access denied") ||
        strstr(output, "permission denied") ||
        strstr(output, "ERROR 1142") ||
        strstr(output, "ERROR 1227")) {
        report->result = SANDBOX_RESULT_SUSPICIOUS;
        report->risk_score += 15;
        strcat(report->detected_patterns, ",access_violation");
    }

    if (strstr(output, "FILE privilege") ||
        strstr(output, "File '") ||
        strstr(output, "Can't get stat of")) {
        report->risk_score += 25;
        if (report->result < SANDBOX_RESULT_SUSPICIOUS) {
            report->result = SANDBOX_RESULT_SUSPICIOUS;
        }
        strcat(report->detected_patterns, ",file_access_attempt");
    }

    if (strstr(output, "MySQL server error") ||
        strstr(output, "Segmentation fault") ||
        strstr(output, "Aborted")) {
        report->risk_score += 10;
        strcat(report->detected_patterns, ",server_error");
    }

    if (strstr(output, "information_schema") ||
        strstr(output, "INFORMATION_SCHEMA") ||
        strstr(output, "mysql.user")) {
        report->risk_score += 35;
        report->result = SANDBOX_RESULT_DANGEROUS;
        strcat(report->detected_patterns, ",schema_exposed");
    }
}

sandbox_report_t sandbox_execute(sandbox_ctx_t *ctx, const char *sql) {
    sandbox_report_t report = {0};
    report.result = SANDBOX_RESULT_UNKNOWN;
    report.execute_time = time(NULL);
    report.risk_score = 0;
    report.detected_patterns[0] = '\0';
    report.output[0] = '\0';
    report.error_msg[0] = '\0';

    if (!ctx || !sql || strlen(sql) == 0) {
        strcpy(report.error_msg, "Invalid context or SQL");
        report.result = SANDBOX_RESULT_ERROR;
        return report;
    }

    struct timeval start, end;
    gettimeofday(&start, NULL);

    check_suspicious_patterns(&report, sql);

    if (!ctx->enabled) {
        strncpy(report.output, "Sandbox execution simulated", sizeof(report.output) - 1);
        gettimeofday(&end, NULL);
        report.execution_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
        return report;
    }

    pthread_mutex_lock(&ctx->mutex);

    pid_t pid = fork();
    if (pid < 0) {
        strcpy(report.error_msg, "Fork failed");
        report.result = SANDBOX_RESULT_ERROR;
        pthread_mutex_unlock(&ctx->mutex);
        return report;
    }

    if (pid == 0) {
        struct rlimit rl;

        rl.rlim_cur = 1;
        rl.rlim_max = 1;
        setrlimit(RLIMIT_CPU, &rl);

        rl.rlim_cur = SANDBOX_MEMORY_MB * 1024 * 1024;
        rl.rlim_max = SANDBOX_MEMORY_MB * 1024 * 1024;
        setrlimit(RLIMIT_AS, &rl);

        rl.rlim_cur = 10;
        rl.rlim_max = 10;
        setrlimit(RLIMIT_NPROC, &rl);

        rl.rlim_cur = 0;
        rl.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &rl);

        char cmd[8192];
        snprintf(cmd, sizeof(cmd),
                 "mysql -h%s -P%d -u%s -p%s -D%s --connect-timeout=3 --execute=\"%s\" 2>&1 | head -c %d",
                 ctx->mysql_host, ctx->mysql_port,
                 ctx->mysql_user, ctx->mysql_pass, ctx->mysql_db,
                 sql, SANDBOX_MAX_OUTPUT - 1);

        FILE *fp = popen(cmd, "r");
        if (fp) {
            char buf[1024];
            size_t total = 0;
            while (fgets(buf, sizeof(buf), fp) && total < SANDBOX_MAX_OUTPUT - 1) {
                strncat(report.output, buf, SANDBOX_MAX_OUTPUT - total - 1);
                total += strlen(buf);
            }
            pclose(fp);
        }

        exit(0);
    } else {
        int status;
        int waited = 0;
        int timeout_ms = SANDBOX_TIMEOUT * 1000;

        while (waited < timeout_ms) {
            int ret = waitpid(pid, &status, WNOHANG);
            if (ret == pid) break;
            if (ret < 0) break;
            usleep(10000);
            waited += 10;
        }

        if (waited >= timeout_ms) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            report.result = SANDBOX_RESULT_TIMEOUT;
            report.risk_score += 40;
            strcat(report.detected_patterns, ",timeout_possible_blind_injection");
        }

        gettimeofday(&end, NULL);
        report.execution_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;

        if (report.execution_ms > 3000) {
            report.risk_score += 20;
            strcat(report.detected_patterns, ",slow_execution");
        }

        analyze_sandbox_output(&report, report.output);

        pthread_mutex_unlock(&ctx->mutex);
    }

    return report;
}

int sandbox_analyze_result(sandbox_report_t *report, const char *sql) {
    if (!report) return -1;

    int score = report->risk_score;

    if (report->result == SANDBOX_RESULT_TIMEOUT) {
        score += 50;
    }

    if (strlen(report->output) > 10000) {
        score += 10;
    }

    return score;
}
