#ifndef UTILS_H
#define UTILS_H

#include "config.h"
#include <stdint.h>
#include <stddef.h>

void die(const char *fmt, ...);
void info(const char *fmt, ...);
void warn(const char *fmt, ...);
void debug(const char *fmt, ...);

char *rtrim(char *s);
char *ltrim(char *s);
char *trim(char *s);

int  hex_dump(const uint8_t *buf, size_t len, char *out, size_t out_size);

uint32_t ntoh3(const uint8_t *p);
uint64_t ntoh_lenenc_int(const uint8_t *p, size_t len, size_t *consumed);

#endif
