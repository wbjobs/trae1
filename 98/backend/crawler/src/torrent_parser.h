#pragma once
#include "config.h"
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/bdecode.hpp>
#include <string>
#include <optional>

namespace bt_monitor {

class TorrentParser {
public:
    static std::optional<TorrentMeta> parse_from_magnet(const std::string& magnet_uri);
    static std::optional<TorrentMeta> parse_from_infohash(const std::string& infohash);
    static std::optional<TorrentMeta> parse_from_torrent_file(const std::string& file_path);
    static std::optional<TorrentMeta> parse_from_bencoded_data(const std::string& data);
};

}
