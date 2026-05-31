#include "sctp_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

static void print_usage(const char *prog_name)
{
    printf("Usage:\n");
    printf("  %s recv [options] <output_dir>\n", prog_name);
    printf("  %s send [options] <file> <remote_ip>\n", prog_name);
    printf("\nCommon options:\n");
    printf("  -p, --port PORT          Port number (default: 9000)\n");
    printf("  -b, --bind ADDR          Bind address (recv mode, default: 0.0.0.0)\n");
    printf("  -l, --local IPS          Local addresses (send mode, comma-separated)\n");
    printf("  -r, --reverse            Enable reverse transfer\n");
    printf("  -R, --resume             Enable resume support\n");
    printf("      --latency-diff MS    Latency difference threshold in ms\n");
    printf("                           (default: 40ms)\n");
    printf("      --plot-graph         Enable real-time quality graph\n");
    printf("  -h, --help               Show this help\n");
    printf("\nExamples:\n");
    printf("  Receiver: %s recv -p 9000 -R --plot-graph ./downloads\n", prog_name);
    printf("  Sender:   %s send -l 192.168.1.100,10.0.0.50 -R --plot-graph bigfile.iso 192.168.1.200\n", prog_name);
}

int main(int argc, char *argv[])
{
    int port = 9000;
    char bind_addr[MAX_IP_LEN] = "0.0.0.0";
    char local_addrs_str[256] = "";
    bool reverse = false;
    bool resume = false;
    bool plot_graph = false;
    uint32_t latency_diff_ms = DEFAULT_LATENCY_DIFF_MS;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    bool is_send = (strcmp(mode, "send") == 0);
    bool is_recv = (strcmp(mode, "recv") == 0);

    if (!is_send && !is_recv) {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        print_usage(argv[0]);
        return 1;
    }

    static struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {"bind", required_argument, 0, 'b'},
        {"local", required_argument, 0, 'l'},
        {"reverse", no_argument, 0, 'r'},
        {"resume", no_argument, 0, 'R'},
        {"latency-diff", required_argument, 0, 1000},
        {"plot-graph", no_argument, 0, 1001},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    optind = 2;
    while ((opt = getopt_long(argc, argv, "p:b:l:rRh",
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'b':
                strncpy(bind_addr, optarg, MAX_IP_LEN - 1);
                break;
            case 'l':
                strncpy(local_addrs_str, optarg, sizeof(local_addrs_str) - 1);
                break;
            case 'r':
                reverse = true;
                break;
            case 'R':
                resume = true;
                break;
            case 1000:
                latency_diff_ms = (uint32_t)atoi(optarg);
                if (latency_diff_ms == 0)
                    latency_diff_ms = 1;
                break;
            case 1001:
                plot_graph = true;
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    int remaining_args = argc - optind;

    if (crc32c_init() < 0) {
        fprintf(stderr, "Failed to initialize CRC32C\n");
        return 1;
    }

    if (is_recv) {
        if (remaining_args < 1) {
            fprintf(stderr, "Output directory required\n");
            print_usage(argv[0]);
            return 1;
        }
        const char *output_dir = argv[optind];
        return receiver_run(output_dir, bind_addr, port,
                            reverse, resume, latency_diff_ms, plot_graph);

    } else {
        if (remaining_args < 2) {
            fprintf(stderr, "File and remote IP required\n");
            print_usage(argv[0]);
            return 1;
        }
        const char *filename = argv[optind];
        const char *remote_addr = argv[optind + 1];

        const char *local_addrs[MAX_PATHS] = {NULL};
        int num_local = 0;

        if (strlen(local_addrs_str) > 0) {
            char *copy = strdup(local_addrs_str);
            char *token = strtok(copy, ",");
            while (token != NULL && num_local < MAX_PATHS) {
                local_addrs[num_local++] = token;
                token = strtok(NULL, ",");
            }
            free(copy);
        }

        return sender_run(filename, remote_addr, port,
                          local_addrs, num_local,
                          reverse, resume, latency_diff_ms, plot_graph);
    }
}
