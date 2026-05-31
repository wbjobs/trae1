#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

extern int g_verbose;

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

void info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[warn] ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void debug(const char *fmt, ...) {
    if (!g_verbose) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[debug] ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

char *rtrim(char *s) {
    if (!s) return s;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
    return s;
}

char *ltrim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

char *trim(char *s) {
    return rtrim(ltrim(s));
}

int hex_dump(const uint8_t *buf, size_t len, char *out, size_t out_size) {
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 4 < out_size; i++) {
        int n = snprintf(out + pos, out_size - pos, "%02x", buf[i]);
        if (n <= 0) break;
        pos += (size_t)n;
    }
    if (pos < out_size) out[pos] = '\0';
    return (int)pos;
}

uint32_t ntoh3(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

uint64_t ntoh_lenenc_int(const uint8_t *p, size_t len, size_t *consumed) {
    if (len < 1) { if (consumed) *consumed = 0; return 0; }
    uint8_t first = p[0];
    if (first < 0xfb) { if (consumed) *consumed = 1; return first; }
    if (first == 0xfc) {
        if (len < 3) { if (consumed) *consumed = 0; return 0; }
        if (consumed) *consumed = 3;
        return (uint64_t)p[1] | ((uint64_t)p[2] << 8);
    }
    if (first == 0xfd) {
        if (len < 4) { if (consumed) *consumed = 0; return 0; }
        if (consumed) *consumed = 4;
        return (uint64_t)p[1] | ((uint64_t)p[2] << 8) | ((uint64_t)p[3] << 16);
    }
    if (first == 0xfe) {
        if (len < 9) { if (consumed) *consumed = 0; return 0; }
        if (consumed) *consumed = 9;
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v |= ((uint64_t)p[1 + i]) << (8 * i);
        return v;
    }
    if (consumed) *consumed = 1;
    return 0;
}
