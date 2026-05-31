#include "dht_crawler.h"
#include "torrent_parser.h"
#include <iostream>
#include <csignal>
#include <cstring>
#include <string>
#include <getopt.h>

static bt_monitor::DHTCrawler* g_crawler = nullptr;

void signal_handler(int signal) {
    if (g_crawler) {
        std::cout << "\n[Main] Received signal " << signal << ", stopping..." << std::endl;
        g_crawler->stop();
    }
    exit(0);
}

void print_usage(const char* prog) {
    std::cout << "BT DHT Crawler\n"
              << "Usage: " << prog << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  --crawl-interval SEC   Crawl interval in seconds (default: 60)\n"
              << "  --listen-port PORT     Listen port (default: 6881)\n"
              << "  --dht-port PORT        DHT port (default: 6882)\n"
              << "  --api-endpoint URL     API endpoint URL (default: http://127.0.0.1:5000/api/infohash)\n"
              << "  --db-path PATH         SQLite database path (default: ../data/bt_monitor.db)\n"
              << "  --max-infohash NUM     Max infohashes per cycle (default: 500)\n"
              << "  --help                 Show this help\n";
}

int main(int argc, char* argv[]) {
    bt_monitor::Config config;

    static struct option long_options[] = {
        {"crawl-interval", required_argument, 0, 'i'},
        {"listen-port",    required_argument, 0, 'l'},
        {"dht-port",       required_argument, 0, 'd'},
        {"api-endpoint",   required_argument, 0, 'a'},
        {"db-path",        required_argument, 0, 'b'},
        {"max-infohash",   required_argument, 0, 'm'},
        {"help",           no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "i:l:d:a:b:m:h", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'i':
            config.crawl_interval_sec = std::atoi(optarg);
            break;
        case 'l':
            config.listen_port = std::atoi(optarg);
            break;
        case 'd':
            config.dht_port = std::atoi(optarg);
            break;
        case 'a':
            config.api_endpoint = optarg;
            break;
        case 'b':
            config.database_path = optarg;
            break;
        case 'm':
            config.max_infohashes_per_cycle = std::atoi(optarg);
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[Main] Starting BT DHT Crawler..." << std::endl;
    std::cout << "[Main] Crawl interval: " << config.crawl_interval_sec << "s" << std::endl;
    std::cout << "[Main] Listen port: " << config.listen_port << std::endl;
    std::cout << "[Main] DHT port: " << config.dht_port << std::endl;
    std::cout << "[Main] API endpoint: " << config.api_endpoint << std::endl;
    std::cout << "[Main] DB path: " << config.database_path << std::endl;

    bt_monitor::DHTCrawler crawler(config);
    g_crawler = &crawler;

    crawler.on_infohash([](const bt_monitor::InfohashRecord& record) {
        std::cout << "[Main] New infohash: " << record.infohash
                  << " from " << record.source_ip << ":" << record.source_port << std::endl;
    });

    crawler.start();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
