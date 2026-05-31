#include "whitelist.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define MAX_WHITELIST_IPS 1024
#define WHITELIST_FILE CONFIG_FILE

typedef struct whitelist_entry {
    uint32_t ip_addr;
    uint32_t ip_mask;
    struct whitelist_entry *next;
} whitelist_entry_t;

static whitelist_entry_t *g_whitelist = NULL;
static pthread_mutex_t g_whitelist_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t g_last_whitelist_mtime = 0;
static time_t g_last_rules_mtime = 0;

static uint32_t parse_ip(const char *ip_str) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str, &addr) == 1) {
        return ntohl(addr.s_addr);
    }
    return 0;
}

static int parse_whitelist_line(const char *line, uint32_t *ip_out, uint32_t *mask_out) {
    char ip_str[64];
    char mask_str[64] = "255.255.255.255";

    strncpy(ip_str, line, sizeof(ip_str) - 1);
    ip_str[sizeof(ip_str) - 1] = '\0';

    char *space = strchr(ip_str, '/');
    if (space) {
        *space = '\0';
        strncpy(mask_str, space + 1, sizeof(mask_str) - 1);
    }

    *ip_out = parse_ip(ip_str);

    if (strcmp(mask_str, "255.255.255.255") == 0) {
        *mask_out = 0xFFFFFFFF;
    } else if (strcmp(mask_str, "255.255.255.0") == 0) {
        *mask_out = 0xFFFFFF00;
    } else if (strcmp(mask_str, "255.255.0.0") == 0) {
        *mask_out = 0xFFFF0000;
    } else if (strcmp(mask_str, "255.0.0.0") == 0) {
        *mask_out = 0xFF000000;
    } else {
        struct in_addr mask_addr;
        if (inet_pton(AF_INET, mask_str, &mask_addr) == 1) {
            *mask_out = ntohl(mask_addr.s_addr);
        } else {
            int cidr = atoi(mask_str);
            if (cidr > 0 && cidr <= 32) {
                *mask_out = cidr == 32 ? 0xFFFFFFFF : (0xFFFFFFFF << (32 - cidr));
            } else {
                *mask_out = 0xFFFFFFFF;
            }
        }
    }

    return 0;
}

static void whitelist_clear(void) {
    whitelist_entry_t *entry = g_whitelist;
    while (entry) {
        whitelist_entry_t *next = entry->next;
        free(entry);
        entry = next;
    }
    g_whitelist = NULL;
}

static int whitelist_load_from_file(void) {
    FILE *fp = fopen(WHITELIST_FILE, "r");
    if (!fp) {
        return -1;
    }

    char line[256];
    int count = 0;

    while (fgets(line, sizeof(line), fp) && count < MAX_WHITELIST_IPS) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;

        if (*p == '#' || *p == '\0' || *p == '\n') {
            continue;
        }

        char *end = p + strlen(p) - 1;
        while (end > p && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';

        if (*p == '\0') continue;

        uint32_t ip_addr, ip_mask;
        if (parse_whitelist_line(p, &ip_addr, &ip_mask) == 0) {
            whitelist_entry_t *entry = malloc(sizeof(whitelist_entry_t));
            if (entry) {
                entry->ip_addr = ip_addr;
                entry->ip_mask = ip_mask;
                entry->next = g_whitelist;
                g_whitelist = entry;
                count++;
            }
        }
    }

    fclose(fp);
    return count;
}

void whitelist_init(void) {
    pthread_mutex_lock(&g_whitelist_mutex);
    whitelist_load_from_file();
    pthread_mutex_unlock(&g_whitelist_mutex);
}

void whitelist_destroy(void) {
    pthread_mutex_lock(&g_whitelist_mutex);
    whitelist_clear();
    pthread_mutex_unlock(&g_whitelist_mutex);
    pthread_mutex_destroy(&g_whitelist_mutex);
}

int reload_whitelist(void) {
    pthread_mutex_lock(&g_whitelist_mutex);

    struct stat st;
    if (stat(WHITELIST_FILE, &st) == 0) {
        if (st.st_mtime > g_last_whitelist_mtime) {
            whitelist_clear();
            whitelist_load_from_file();
            g_last_whitelist_mtime = st.st_mtime;
        }
    }

    pthread_mutex_unlock(&g_whitelist_mutex);
    return 0;
}

int reload_rules(void) {
    struct stat st;
    if (stat(RULES_FILE, &st) == 0) {
        if (st.st_mtime > g_last_rules_mtime) {
            g_last_rules_mtime = st.st_mtime;
        }
    }
    return 0;
}

bool is_whitelisted(const char *ip) {
    if (!ip) return false;

    uint32_t check_ip = parse_ip(ip);
    if (check_ip == 0) return false;

    pthread_mutex_lock(&g_whitelist_mutex);

    whitelist_entry_t *entry = g_whitelist;
    while (entry) {
        if ((check_ip & entry->ip_mask) == (entry->ip_addr & entry->ip_mask)) {
            pthread_mutex_unlock(&g_whitelist_mutex);
            return true;
        }
        entry = entry->next;
    }

    pthread_mutex_unlock(&g_whitelist_mutex);
    return false;
}

int whitelist_add(const char *ip) {
    uint32_t ip_addr, ip_mask;
    if (parse_whitelist_line(ip, &ip_addr, &ip_mask) != 0) {
        return -1;
    }

    pthread_mutex_lock(&g_whitelist_mutex);

    whitelist_entry_t *entry = malloc(sizeof(whitelist_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&g_whitelist_mutex);
        return -1;
    }

    entry->ip_addr = ip_addr;
    entry->ip_mask = ip_mask;
    entry->next = g_whitelist;
    g_whitelist = entry;

    pthread_mutex_unlock(&g_whitelist_mutex);
    return 0;
}

int whitelist_remove(const char *ip) {
    uint32_t ip_addr, ip_mask;
    if (parse_whitelist_line(ip, &ip_addr, &ip_mask) != 0) {
        return -1;
    }

    pthread_mutex_lock(&g_whitelist_mutex);

    whitelist_entry_t **prev = &g_whitelist;
    whitelist_entry_t *entry = g_whitelist;

    while (entry) {
        if (entry->ip_addr == ip_addr && entry->ip_mask == ip_mask) {
            *prev = entry->next;
            free(entry);
            pthread_mutex_unlock(&g_whitelist_mutex);
            return 0;
        }
        prev = &entry->next;
        entry = entry->next;
    }

    pthread_mutex_unlock(&g_whitelist_mutex);
    return -1;
}
