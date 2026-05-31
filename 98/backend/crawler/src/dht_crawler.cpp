#include "dht_crawler.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>
#include <ctime>
#include <algorithm>
#include <cpr/cpr.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

namespace bt_monitor {

BloomFilter::BloomFilter(size_t num_elements, int num_hashes)
    : num_bits_(num_elements * 8),
      num_hashes_(num_hashes),
      bit_array_(num_elements * 8, false) {
}

size_t BloomFilter::hash(const std::string& item, int seed) const {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    std::string input = item + ":" + std::to_string(seed);
    SHA256(reinterpret_cast<const unsigned char*>(input.data()),
           input.size(), hash);
    size_t result = 0;
    for (int i = 0; i < 8 && i < SHA256_DIGEST_LENGTH; ++i) {
        result = (result << 8) | hash[i];
    }
    return result % num_bits_;
}

void BloomFilter::add(const std::string& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < num_hashes_; ++i) {
        bit_array_[hash(item, i)] = true;
    }
    num_added_++;
}

bool BloomFilter::contains(const std::string& item) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < num_hashes_; ++i) {
        if (!bit_array_[hash(item, i)]) {
            return false;
        }
    }
    return true;
}

void BloomFilter::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fill(bit_array_.begin(), bit_array_.end(), false);
    num_added_ = 0;
}

size_t BloomFilter::estimated_count() const {
    double m = static_cast<double>(num_bits_);
    double k = static_cast<double>(num_hashes_);
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (bool b : bit_array_) {
        if (b) count++;
    }
    double x = static_cast<double>(count);
    if (x >= m) return num_added_.load();
    return static_cast<size_t>(-(m / k) * std::log(1.0 - x / m));
}

DHTCrawler::DHTCrawler(const Config& config)
    : config_(config),
      session_(lt::session_params{}),
      bloom_filter_(config_.bloom_filter_size, config_.bloom_filter_hash_count) {
    lt::settings_pack settings;
    settings.set_int(lt::settings_pack::alert_mask,
        lt::alert_category::dht | lt::alert_category::peer);
    settings.set_int(lt::settings_pack::dht_upload_rate_limit, 10000);
    settings.set_int(lt::settings_pack::dht_download_rate_limit, 10000);
    settings.set_bool(lt::settings_pack::enable_dht, true);
    settings.set_bool(lt::settings_pack::enable_lsd, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_int(lt::settings_pack::torrent_upload_limit, 0);
    settings.set_int(lt::settings_pack::torrent_download_limit, 0);
    session_.apply_settings(settings);

    lt::error_code ec;
    lt::tcp::endpoint ep(lt::address_v4::from_string(config_.listen_interface),
                          config_.listen_port);
    session_.listen_on(ep, ec);
    if (ec) {
        std::cerr << "[DHT] Failed to listen on " << config_.listen_interface
                  << ":" << config_.listen_port << ": " << ec.message() << std::endl;
    }
}

DHTCrawler::~DHTCrawler() {
    stop();
}

void DHTCrawler::on_infohash(std::function<void(const InfohashRecord&)> callback) {
    infohash_callback_ = std::move(callback);
}

void DHTCrawler::start() {
    running_ = true;

    if (!config_.bootstrap_node_list.empty()) {
        config_.current_bootstrap_index = 0;
        config_.bootstrap_node = config_.bootstrap_node_list[0];
    }

    session_.add_dht_node(lt::dht_node_config{
        lt::udp::endpoint(boost::asio::ip::address_v4::from_string(config_.listen_interface),
                          config_.dht_port),
        config_.bootstrap_node
    });

    inject_route_table();

    session_thread_ = std::thread([this]() { session_loop(); });

    std::cout << "[DHT] Crawler started on port " << config_.listen_port
              << ", DHT port " << config_.dht_port << std::endl;
    std::cout << "[DHT] Bloom filter: " << config_.bloom_filter_size << " bits, "
              << config_.bloom_filter_hash_count << " hashes" << std::endl;
    std::cout << "[DHT] Blacklist TTL: " << config_.blacklist_ttl_seconds << "s" << std::endl;
    std::cout << "[DHT] Failure rate threshold: "
              << (config_.failure_rate_threshold * 100) << "%" << std::endl;
}

void DHTCrawler::stop() {
    running_ = false;
    if (session_thread_.joinable()) {
        session_thread_.join();
    }
}

void DHTCrawler::session_loop() {
    auto last_crawl = std::chrono::steady_clock::now();
    auto last_cleanup = std::chrono::steady_clock::now();

    while (running_) {
        handle_alerts();

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_crawl).count();

        if (elapsed >= config_.crawl_interval_sec) {
            crawl_cycle();
            last_crawl = now;
        }

        auto cleanup_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_cleanup).count();
        if (cleanup_elapsed >= 3600) {
            cleanup_expired_blacklist();
            check_failure_rate_and_switch();
            last_cleanup = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void DHTCrawler::crawl_cycle() {
    std::cout << "[DHT] === Crawl cycle start === collected: "
              << collected_infohashes_.size()
              << ", bloom_estimated: " << bloom_filter_.estimated_count()
              << ", blacklist: " << get_blacklist_count()
              << ", failure_rate: " << (get_failure_rate() * 100) << "%"
              << std::endl;

    inject_route_table();
    probe_nodes();

    std::cout << "[DHT] === Crawl cycle end === nodes probed: "
              << probed_nodes_.size() << std::endl;
}

void DHTCrawler::inject_route_table() {
    int start_idx = config_.current_bootstrap_index;
    int count = 0;

    for (size_t i = 0; i < config_.bootstrap_node_list.size() && count < 4; ++i) {
        int idx = (start_idx + i) % config_.bootstrap_node_list.size();
        const auto& node = config_.bootstrap_node_list[idx];

        try {
            session_.add_dht_node(lt::dht_node_config{
                lt::udp::endpoint(boost::asio::ip::address_v4::any(), 0),
                node
            });
            count++;
        } catch (...) {
            continue;
        }
    }

    for (const auto& node : config_.bootstrap_node_list) {
        if (count >= 6) break;
        try {
            session_.add_dht_node(lt::dht_node_config{
                lt::udp::endpoint(boost::asio::ip::address_v4::any(), 0),
                node
            });
            count++;
        } catch (...) {
            continue;
        }
    }

    session_.dht_state();
    session_.dht_announce();
}

void DHTCrawler::probe_nodes() {
    auto state = session_.dht_state();
    if (!state) return;

    auto dht_state = state->get();

    int probed_count = 0;
    for (const auto& node : dht_state.nodes) {
        std::string node_key = node.ip.to_string() + ":" + std::to_string(node.port);
        {
            std::lock_guard<std::mutex> lock(node_mutex_);
            if (probed_nodes_.count(node_key)) continue;
            probed_nodes_.insert(node_key);
        }

        lt::dht_lookup lookup;
        lookup.node_id = lt::dht::node_id(lt::dht::generate_random_id());
        lookup.target = lt::dht::node_id(lt::dht::generate_random_id());

        try {
            session_.dht_direct_request(node.ep, lt::dht::ping(lookup.node_id),
                [](lt::dht_direct_response const& response) {
                    if (response.response.has_key_value("id")) {
                        auto id = response.response.dict_find_string_value("id");
                        if (id) {
                            std::cout << "[DHT] Node probed: "
                                      << response.source.address().to_string()
                                      << ":" << response.source.port() << std::endl;
                        }
                    }
                });
        } catch (...) {
            continue;
        }

        if (++probed_count >= 50) break;
    }

    if (probed_nodes_.size() > 10000) {
        std::lock_guard<std::mutex> lock(node_mutex_);
        auto it = probed_nodes_.begin();
        std::advance(it, probed_nodes_.size() / 2);
        probed_nodes_.erase(probed_nodes_.begin(), it);
    }
}

void DHTCrawler::handle_alerts() {
    std::vector<lt::alert*> alerts;
    session_.pop_alerts(&alerts);

    for (auto alert : alerts) {
        switch (alert->type()) {
        case lt::dht_get_peers_alert::alert_type:
            process_dht_get_peers(static_cast<lt::dht_get_peers_alert*>(alert));
            break;
        case lt::dht_announce_alert::alert_type:
            process_dht_announce(static_cast<lt::dht_announce_alert*>(alert));
            break;
        default:
            break;
        }
    }
}

void DHTCrawler::process_dht_get_peers(lt::dht_get_peers_alert* alert) {
    lt::sha1_hash ih = alert->info_hash;
    std::string infohash_str = lt::aux::to_hex(ih.to_string());

    if (bloom_filter_.contains(infohash_str) && is_blacklisted(infohash_str)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(infohash_mutex_);
        if (collected_infohashes_.count(infohash_str)) return;
        if ((int)collected_infohashes_.size() >= config_.max_infohashes_per_cycle) return;
        collected_infohashes_.insert(infohash_str);
    }

    bloom_filter_.add(infohash_str);

    estimate_seeders(infohash_str, 1);

    InfohashRecord record;
    record.infohash = infohash_str;
    record.source_ip = alert->ip.to_string();
    record.source_port = alert->port;
    record.timestamp = std::time(nullptr);
    record.event_type = "get_peers";
    record.announce_count = 1;

    std::cout << "[DHT] Collected infohash: " << infohash_str
              << " from " << record.source_ip << ":" << record.source_port << std::endl;

    if (infohash_callback_) {
        infohash_callback_(record);
    }

    send_infohash_to_api(record);
}

void DHTCrawler::process_dht_announce(lt::dht_announce_alert* alert) {
    lt::sha1_hash ih = alert->info_hash;
    std::string infohash_str = lt::aux::to_hex(ih.to_string());

    if (bloom_filter_.contains(infohash_str) && is_blacklisted(infohash_str)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(announce_mutex_);
        announce_counter_[infohash_str]++;
    }

    {
        std::lock_guard<std::mutex> lock(infohash_mutex_);
        if (collected_infohashes_.count(infohash_str)) return;
        if ((int)collected_infohashes_.size() >= config_.max_infohashes_per_cycle) return;
        collected_infohashes_.insert(infohash_str);
    }

    bloom_filter_.add(infohash_str);

    int announce_count = 1;
    {
        std::lock_guard<std::mutex> lock(announce_mutex_);
        announce_count = announce_counter_[infohash_str];
    }

    estimate_seeders(infohash_str, announce_count);

    InfohashRecord record;
    record.infohash = infohash_str;
    record.source_ip = alert->ip.to_string();
    record.source_port = alert->port;
    record.timestamp = std::time(nullptr);
    record.event_type = "announce";
    record.announce_count = announce_count;

    std::cout << "[DHT] Announced infohash: " << infohash_str
              << " from " << record.source_ip << ":" << record.source_port
              << " (announce_count=" << announce_count << ")" << std::endl;

    if (infohash_callback_) {
        infohash_callback_(record);
    }

    send_infohash_to_api(record);
}

void DHTCrawler::send_infohash_to_api(const InfohashRecord& record) {
    try {
        std::string body = "{\"infohash\":\"" + record.infohash + "\","
                           "\"source_ip\":\"" + record.source_ip + "\","
                           "\"source_port\":" + std::to_string(record.source_port) + ","
                           "\"timestamp\":" + std::to_string(record.timestamp) + ","
                           "\"event_type\":\"" + record.event_type + "\","
                           "\"announce_count\":" + std::to_string(record.announce_count) + "}";

        cpr::Response r = cpr::Post(
            cpr::Url{config_.api_endpoint},
            cpr::Body{body},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Timeout{5000}
        );

        if (r.status_code == 410) {
            add_to_blacklist(record.infohash, "api_blacklisted");
        } else if (r.status_code != 200 && r.status_code != 201) {
            std::cerr << "[DHT] API post failed: " << r.status_code << std::endl;
            failure_count_++;
        } else {
            success_count_++;
            consecutive_failures_ = 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "[DHT] API post exception: " << e.what() << std::endl;
        failure_count_++;
        consecutive_failures_++;
    }
}

bool DHTCrawler::is_blacklisted(const std::string& infohash) const {
    std::lock_guard<std::mutex> lock(blacklist_mutex_);
    auto it = blacklist_.find(infohash);
    if (it != blacklist_.end()) {
        int64_t now = std::time(nullptr);
        if (it->second.expires_at > now) {
            return true;
        }
    }
    return false;
}

void DHTCrawler::add_to_blacklist(const std::string& infohash, const std::string& reason) {
    int64_t now = std::time(nullptr);
    BlacklistEntry entry;
    entry.infohash = infohash;
    entry.reason = reason;
    entry.added_at = now;
    entry.expires_at = now + config_.blacklist_ttl_seconds;

    {
        std::lock_guard<std::mutex> lock(blacklist_mutex_);
        blacklist_[infohash] = entry;
    }

    bloom_filter_.add(infohash);

    std::cout << "[DHT] Blacklisted: " << infohash
              << " (reason: " << reason
              << ", expires: " << entry.expires_at << ")" << std::endl;
}

void DHTCrawler::cleanup_expired_blacklist() {
    int64_t now = std::time(nullptr);
    std::vector<std::string> expired;

    {
        std::lock_guard<std::mutex> lock(blacklist_mutex_);
        for (const auto& [infohash, entry] : blacklist_) {
            if (entry.expires_at <= now) {
                expired.push_back(infohash);
            }
        }
        for (const auto& h : expired) {
            blacklist_.erase(h);
        }
    }

    if (!expired.empty()) {
        std::cout << "[DHT] Cleaned " << expired.size() << " expired blacklist entries" << std::endl;
    }
}

int DHTCrawler::get_blacklist_count() const {
    std::lock_guard<std::mutex> lock(blacklist_mutex_);
    return blacklist_.size();
}

double DHTCrawler::get_failure_rate() const {
    int total = success_count_.load() + failure_count_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(failure_count_.load()) / total;
}

void DHTCrawler::check_failure_rate_and_switch() {
    double rate = get_failure_rate();
    if (rate >= config_.failure_rate_threshold) {
        std::cout << "[DHT] WARNING: Failure rate " << (rate * 100) << "% exceeds threshold "
                  << (config_.failure_rate_threshold * 100) << "%" << std::endl;
        std::cout << "[DHT] Switching bootstrap node and resetting failure stats..." << std::endl;

        switch_bootstrap_node();

        success_count_ = 0;
        failure_count_ = 0;
        consecutive_failures_ = 0;
    }
}

void DHTCrawler::switch_bootstrap_node() {
    if (config_.bootstrap_node_list.empty()) return;

    config_.current_bootstrap_index =
        (config_.current_bootstrap_index + 1) % config_.bootstrap_node_list.size();
    config_.bootstrap_node = config_.bootstrap_node_list[config_.current_bootstrap_index];

    std::cout << "[DHT] Switched to bootstrap node: "
              << config_.bootstrap_node
              << " (index " << config_.current_bootstrap_index << ")" << std::endl;

    try {
        session_.add_dht_node(lt::dht_node_config{
            lt::udp::endpoint(boost::asio::ip::address_v4::any(), 0),
            config_.bootstrap_node
        });
    } catch (...) {
        std::cerr << "[DHT] Failed to add new bootstrap node" << std::endl;
    }
}

void DHTCrawler::estimate_seeders(const std::string& infohash, int announce_count) {
    int estimated = announce_count * (2 + std::rand() % 14);
    estimated = std::min(estimated, 5000);

    std::cout << "[DHT] Estimated seeders for " << infohash.substr(0, 12)
              << "...: " << estimated << std::endl;
}

}
