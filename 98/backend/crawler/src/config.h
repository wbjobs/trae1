#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <array>

namespace bt_monitor {

struct Config {
    std::string listen_interface = "0.0.0.0";
    int listen_port = 6881;
    int dht_port = 6882;
    std::string bootstrap_node = "router.bittorrent.com:6881";
    int crawl_interval_sec = 60;
    int max_infohashes_per_cycle = 500;
    std::string api_endpoint = "http://127.0.0.1:5000/api/infohash";
    std::string database_path = "../data/bt_monitor.db";

    int bloom_filter_size = 10000000;
    int bloom_filter_hash_count = 7;
    int blacklist_ttl_seconds = 86400;
    int metadata_timeout_seconds = 15;
    int max_retry_count = 2;
    double failure_rate_threshold = 0.5;
    int announce_count_threshold = 5;

    std::vector<std::string> bootstrap_node_list = {
        "router.bittorrent.com:6881",
        "router.utorrent.com:6881",
        "dht.transmissionbt.com:6881",
        "router.bittorrent.com:6882",
        "dht.libtorrent.org:25401",
        "dht2.transmissionbt.com:6881",
        "router.bittorrent.com:6883",
        "dht.transmissionbt.com:6882"
    };

    int current_bootstrap_index = 0;
};

struct InfohashRecord {
    std::string infohash;
    std::string source_ip;
    uint16_t source_port;
    int64_t timestamp;
    std::string event_type;
    int announce_count = 1;
};

struct TorrentMeta {
    std::string infohash;
    std::string name;
    int64_t total_size;
    std::vector<std::string> files;
    std::vector<std::string> piece_hashes;
    int piece_length;
    int piece_count;
    int64_t timestamp;
};

struct BlacklistEntry {
    std::string infohash;
    std::string reason;
    int64_t added_at;
    int64_t expires_at;
};

}
