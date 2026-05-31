#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef int ssize_t;
#define CLOSE_SOCKET(s) closesocket(s)
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define CLOSE_SOCKET(s) close(s)
#endif

#define MAX_SESSIONS 10
#define MAX_PATH_LENGTH 4096
#define MAX_SHARE_NAME 256
#define MAX_USERNAME 256
#define MAX_PASSWORD 256

#define DEFAULT_CREDITS 8192
#define MIN_CREDITS 512
#define MAX_CREDITS 65535
#define DEFAULT_CACHE_SIZE (64 * 1024 * 1024)
#define MIN_CACHE_SIZE (1024 * 1024)
#define CACHE_BLOCK_SIZE (64 * 1024)

typedef struct {
    char share_name[MAX_SHARE_NAME];
    char local_path[MAX_PATH_LENGTH];
    bool encrypt;
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    bool bench_mode;
    char log_path[MAX_PATH_LENGTH];
    int max_credits;
    size_t cache_size;
    bool ransomware_protection;
    bool decoy_files;
    double entropy_threshold;
    bool auto_quarantine;
} Config;

extern Config global_config;

#endif
