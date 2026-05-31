#ifndef CONFIG_H
#define CONFIG_H

#define MYSQL_PORT 3306
#define NFQUEUE_NUM 0
#define MAX_PAYLOAD_SIZE 65535
#define TCP_BUFFER_SIZE (1024 * 1024)
#define MAX_TRACKED_CONNECTIONS 65536
#define CONNECTION_TIMEOUT 300
#define STATS_INTERVAL 60
#define LOG_FILE "/var/log/mysql_firewall.log"
#define CONFIG_FILE "/etc/mysql_firewall/whitelist.conf"
#define RULES_FILE "/etc/mysql_firewall/rules.conf"
#define LEARNED_RULES_FILE "/etc/mysql_firewall/learned_rules.conf"
#define DEFAULT_SANDBOX_HOST "127.0.0.1"
#define DEFAULT_SANDBOX_PORT 3307
#define DEFAULT_SANDBOX_USER "sandbox"
#define DEFAULT_SANDBOX_PASS "sandbox_pass"
#define DEFAULT_SANDBOX_DB "sandbox_db"

#endif
