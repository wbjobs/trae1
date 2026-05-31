#ifndef WHITELIST_H
#define WHITELIST_H

#include <stdbool.h>

void whitelist_init(void);
void whitelist_destroy(void);
int reload_whitelist(void);
int reload_rules(void);
bool is_whitelisted(const char *ip);
int whitelist_add(const char *ip);
int whitelist_remove(const char *ip);

#endif
