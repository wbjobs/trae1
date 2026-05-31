#include "common.h"
#include "smb_server.h"
#include "bench.h"
#include <getopt.h>
#include <signal.h>

Config global_config;
SmbServer *global_server = NULL;

void signal_handler(int sig) {
    (void)sig;
    if (global_server) {
        printf("\nReceived shutdown signal, stopping server...\n");
        smb_server_stop(global_server);
    }
}

void print_usage(const char *prog_name) {
    printf("SMB 3.0 Encrypt Proxy Tool (with Ransomware Protection)\n");
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  --smb-share <name>     Specify SMB share name (required)\n");
    printf("  --path <path>          Specify local path to share (required)\n");
    printf("  --encrypt              Force AES-128-GCM encryption\n");
    printf("  --username <user>      Specify username for authentication\n");
    printf("  --password <pass>      Specify password for authentication\n");
    printf("  --credit <num>         Set max credits (default: %d, range: %d-%d)\n", 
           DEFAULT_CREDITS, MIN_CREDITS, MAX_CREDITS);
    printf("  --cache-size <size>    Set cache size in MB (default: 64, min: 1)\n");
    printf("  --ransomware-protect   Enable ransomware detection and protection\n");
    printf("  --decoy-files          Deploy decoy files for early detection\n");
    printf("  --entropy-threshold <v> Set entropy threshold (0.0-1.0, default: 0.9)\n");
    printf("  --no-auto-quarantine   Disable automatic quarantine on detection\n");
    printf("  --bench                Run performance benchmark\n");
    printf("  --log <path>           Specify SQLite log file path (default: smb_access.log)\n");
    printf("  --help                 Show this help message\n");
}

static size_t parse_size_mb(const char *str) {
    char *end;
    unsigned long val = strtoul(str, &end, 10);
    if (end == str || val == 0) {
        return 0;
    }
    return (size_t)val * 1024 * 1024;
}

int parse_arguments(int argc, char *argv[]) {
    memset(&global_config, 0, sizeof(Config));
    strcpy(global_config.log_path, "smb_access.log");
    global_config.max_credits = DEFAULT_CREDITS;
    global_config.cache_size = DEFAULT_CACHE_SIZE;
    global_config.ransomware_protection = false;
    global_config.decoy_files = false;
    global_config.entropy_threshold = 0.9;
    global_config.auto_quarantine = true;

    static struct option long_options[] = {
        {"smb-share", required_argument, 0, 's'},
        {"path", required_argument, 0, 'p'},
        {"encrypt", no_argument, 0, 'e'},
        {"username", required_argument, 0, 'u'},
        {"password", required_argument, 0, 'w'},
        {"credit", required_argument, 0, 'c'},
        {"cache-size", required_argument, 0, 'z'},
        {"ransomware-protect", no_argument, 0, 'r'},
        {"decoy-files", no_argument, 0, 'd'},
        {"entropy-threshold", required_argument, 0, 't'},
        {"no-auto-quarantine", no_argument, 0, 'q'},
        {"bench", no_argument, 0, 'b'},
        {"log", required_argument, 0, 'l'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "s:p:eu:w:c:z:rdt:qbl:h", 
                               long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                strncpy(global_config.share_name, optarg, MAX_SHARE_NAME - 1);
                break;
            case 'p':
                strncpy(global_config.local_path, optarg, MAX_PATH_LENGTH - 1);
                break;
            case 'e':
                global_config.encrypt = true;
                break;
            case 'u':
                strncpy(global_config.username, optarg, MAX_USERNAME - 1);
                break;
            case 'w':
                strncpy(global_config.password, optarg, MAX_PASSWORD - 1);
                break;
            case 'c': {
                int credits = atoi(optarg);
                if (credits < MIN_CREDITS || credits > MAX_CREDITS) {
                    fprintf(stderr, "Error: credit value must be between %d and %d\n", 
                            MIN_CREDITS, MAX_CREDITS);
                    return -1;
                }
                global_config.max_credits = credits;
                break;
            }
            case 'z': {
                size_t size = parse_size_mb(optarg);
                if (size < MIN_CACHE_SIZE) {
                    fprintf(stderr, "Error: cache size must be at least 1 MB\n");
                    return -1;
                }
                global_config.cache_size = size;
                break;
            }
            case 'r':
                global_config.ransomware_protection = true;
                break;
            case 'd':
                global_config.decoy_files = true;
                break;
            case 't': {
                double threshold = atof(optarg);
                if (threshold < 0.0 || threshold > 1.0) {
                    fprintf(stderr, "Error: entropy threshold must be between 0.0 and 1.0\n");
                    return -1;
                }
                global_config.entropy_threshold = threshold;
                break;
            }
            case 'q':
                global_config.auto_quarantine = false;
                break;
            case 'b':
                global_config.bench_mode = true;
                break;
            case 'l':
                strncpy(global_config.log_path, optarg, MAX_PATH_LENGTH - 1);
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    if (!global_config.bench_mode) {
        if (strlen(global_config.share_name) == 0 || strlen(global_config.local_path) == 0) {
            fprintf(stderr, "Error: --smb-share and --path are required (unless --bench is used)\n");
            print_usage(argv[0]);
            return -1;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (parse_arguments(argc, argv) != 0) {
        return 1;
    }

    printf("SMB 3.0 Encrypt Proxy (with Ransomware Protection)\n");
    printf("==============================================\n");

    if (global_config.bench_mode) {
        printf("Benchmark mode enabled\n");
        if (global_config.encrypt) {
            printf("Encryption: AES-128-GCM enabled\n");
        }
        printf("Credits: %d\n", global_config.max_credits);
        printf("Cache Size: %zu MB\n", global_config.cache_size / (1024 * 1024));

        BenchmarkResult result;
        const char *test_path = strlen(global_config.local_path) > 0 ? 
                                global_config.local_path : ".";
        
        if (run_benchmark(&result, test_path, global_config.encrypt) != 0) {
            fprintf(stderr, "Benchmark failed\n");
            return 1;
        }

        print_benchmark_result(&result);
        return 0;
    }

    printf("Share Name: %s\n", global_config.share_name);
    printf("Local Path: %s\n", global_config.local_path);
    printf("Encryption: %s\n", global_config.encrypt ? "AES-128-GCM (forced)" : "Disabled");
    printf("Authentication: %s\n", 
           strlen(global_config.username) > 0 ? global_config.username : "None (guest)");
    printf("Max Credits: %d\n", global_config.max_credits);
    printf("Cache Size: %zu MB\n", global_config.cache_size / (1024 * 1024));
    printf("Log File: %s\n", global_config.log_path);
    
    if (global_config.ransomware_protection) {
        printf("\nRansomware Protection:\n");
        printf("  Status: ENABLED\n");
        printf("  Entropy Threshold: %.2f\n", global_config.entropy_threshold);
        printf("  Decoy Files: %s\n", global_config.decoy_files ? "Yes" : "No");
        printf("  Auto Quarantine: %s\n", global_config.auto_quarantine ? "Yes" : "No");
    }
    
    printf("\nPerformance Optimization:\n");
    printf("  - High-latency network optimization: credits=%d\n", global_config.max_credits);
    printf("  - LRU cache enabled: %zu MB read/write buffer\n", global_config.cache_size / (1024 * 1024));
    printf("\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    SmbServer server;
    global_server = &server;

    if (smb_server_init(&server) != 0) {
        fprintf(stderr, "Failed to initialize SMB server\n");
        return 1;
    }

    if (global_config.ransomware_protection && global_config.decoy_files) {
        ransomware_detector_deploy_decoys(&server.ransomware_detector, global_config.local_path);
    }

    printf("Server started. Press Ctrl+C to stop.\n\n");
    smb_server_start(&server);

    printf("\nServer stopped.\n");
    smb_server_cleanup(&server);
    global_server = NULL;

    return 0;
}
