#ifndef MYSQL_FIREWALL_H
#define MYSQL_FIREWALL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <libnetfilter_queue/libnetfilter_queue.h>
#include <libnetfilter_queue/libnetfilter_queue_ipv4.h>
#include <libnetfilter_queue/pktbuff.h>
#include <linux/netfilter.h>
#include "sandbox.h"
#include "learning.h"

typedef struct {
    uint64_t total_packets;
    uint64_t total_inspections;
    uint64_t total_blocks;
    uint64_t total_allows;
    uint64_t regex_matches;
    uint64_t syntax_matches;
    uint64_t learning_matches;
    uint64_t sandbox_verified;
    uint64_t total_suspicious;
    pthread_mutex_t mutex;
} stats_t;

typedef struct {
    int running;
    int debug;
    int learn_mode;
    int use_sandbox;
    FILE *logfile;
    pthread_mutex_t log_mutex;
    sandbox_ctx_t *sandbox;
    learning_ctx_t *learning;
    char export_path[512];
    char import_path[512];
    char sandbox_host[64];
    int sandbox_port;
    char sandbox_user[64];
    char sandbox_pass[128];
    char sandbox_db[64];
} global_ctx_t;

extern global_ctx_t g_ctx;
extern stats_t g_stats;

void signal_handler(int sig);
int setup_nfqueue(void);
void *stats_thread(void *arg);
void log_message(int level, const char *fmt, ...);
uint16_t calculate_tcp_payload_length(struct nfq_q_handle *qh, struct nfq_data *nfa);
char *extract_tcp_payload(struct nfq_q_handle *qh, struct nfq_data *nfa, int *payload_len);
int handle_packet(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg, struct nfq_data *nfa, void *data);

#endif
