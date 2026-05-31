#pragma once
#include "config.h"
#include <libtorrent/session.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/dht_state.hpp>
#include <libtorrent/dht_lookup.hpp>
#include <libtorrent/dht_direct_response.hpp>
#include <libtorrent/dht_tracker.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <boost/asio.hpp>
#include <thread>
#include <mutex>
#include <atomic>
#include <set>
#include <map>
#include <unordered_set>
#include <queue>
#include <condition_variable>
#include <functional>
#include <vector>
#include <bitset>
#include <cstdint>

namespace bt_monitor {

class BloomFilter {
public:
    explicit BloomFilter(size_t num_elements = 10000000, int num_hashes = 7);

    void add(const std::string& item);
    bool contains(const std::string& item) const;
    void clear();
    size_t estimated_count() const;

private:
    size_t hash(const std::string& item, int seed) const;

    size_t num_bits_;
    int num_hashes_;
    std::vector<bool> bit_array_;
    mutable std::mutex mutex_;
    std::atomic<size_t> num_added_{0};
};

class DHTCrawler {
public:
    explicit DHTCrawler(const Config& config);
    ~DHTCrawler();

    void start();
    void stop();

    void on_infohash(std::function<void(const InfohashRecord&)> callback);

    int get_collected_count() const { return collected_infohashes_.size(); }
    int get_blacklist_count() const;
    double get_failure_rate() const;

private:
    void session_loop();
    void crawl_cycle();
    void inject_route_table();
    void probe_nodes();
    void handle_alerts();
    void process_dht_get_peers(lt::dht_get_peers_alert* alert);
    void process_dht_announce(lt::dht_announce_alert* alert);
    void send_infohash_to_api(const InfohashRecord& record);

    bool is_blacklisted(const std::string& infohash) const;
    void add_to_blacklist(const std::string& infohash, const std::string& reason);
    void cleanup_expired_blacklist();

    void check_failure_rate_and_switch();
    void switch_bootstrap_node();

    void estimate_seeders(const std::string& infohash, int announce_count);

    Config config_;
    lt::session session_;
    std::thread session_thread_;
    std::atomic<bool> running_{false};

    std::set<std::string> collected_infohashes_;
    std::mutex infohash_mutex_;

    std::set<std::string> probed_nodes_;
    std::mutex node_mutex_;

    std::function<void(const InfohashRecord&)> infohash_callback_;

    std::queue<InfohashRecord> infohash_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    BloomFilter bloom_filter_;
    std::map<std::string, BlacklistEntry> blacklist_;
    std::map<std::string, int> retry_count_;
    std::map<std::string, int> announce_counter_;
    mutable std::mutex blacklist_mutex_;
    mutable std::mutex retry_mutex_;
    mutable std::mutex announce_mutex_;

    std::atomic<int> success_count_{0};
    std::atomic<int> failure_count_{0};
    std::atomic<int> consecutive_failures_{0};

    boost::asio::io_context io_context_;
};

}
