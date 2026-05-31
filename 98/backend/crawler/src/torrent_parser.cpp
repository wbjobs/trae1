#include "torrent_parser.h"
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/aux_/to_hex.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>

namespace bt_monitor {

std::optional<TorrentMeta> TorrentParser::parse_from_magnet(const std::string& magnet_uri) {
    try {
        lt::error_code ec;
        lt::torrent_info ti(lt::parse_magnet_uri(magnet_uri), ec);
        if (ec) {
            std::cerr << "[Parser] Failed to parse magnet URI: " << ec.message() << std::endl;
            return std::nullopt;
        }

        TorrentMeta meta;
        meta.infohash = lt::aux::to_hex(ti.info_hash().to_string());
        meta.name = ti.name();
        meta.total_size = ti.total_size();
        meta.piece_length = ti.piece_length();
        meta.piece_count = ti.num_pieces();
        meta.timestamp = std::time(nullptr);

        for (int i = 0; i < ti.num_files(); ++i) {
            auto const& f = ti.file_at(i);
            meta.files.push_back(f.path + " (" + std::to_string(f.size) + " bytes)");
        }

        for (int i = 0; i < ti.num_pieces(); ++i) {
            auto hash = ti.hash_for_piece(i);
            meta.piece_hashes.push_back(lt::aux::to_hex(hash.to_string()));
        }

        return meta;
    } catch (const std::exception& e) {
        std::cerr << "[Parser] Exception parsing magnet: " << e.what() << std::endl;
        return std::nullopt;
    }
}

std::optional<TorrentMeta> TorrentParser::parse_from_infohash(const std::string& infohash) {
    std::string magnet = "magnet:?xt=urn:btih:" + infohash;
    return parse_from_magnet(magnet);
}

std::optional<TorrentMeta> TorrentParser::parse_from_torrent_file(const std::string& file_path) {
    try {
        lt::error_code ec;
        lt::torrent_info ti(file_path, ec);
        if (ec) {
            std::cerr << "[Parser] Failed to parse torrent file: " << ec.message() << std::endl;
            return std::nullopt;
        }

        TorrentMeta meta;
        meta.infohash = lt::aux::to_hex(ti.info_hash().to_string());
        meta.name = ti.name();
        meta.total_size = ti.total_size();
        meta.piece_length = ti.piece_length();
        meta.piece_count = ti.num_pieces();
        meta.timestamp = std::time(nullptr);

        for (int i = 0; i < ti.num_files(); ++i) {
            auto const& f = ti.file_at(i);
            meta.files.push_back(f.path + " (" + std::to_string(f.size) + " bytes)");
        }

        for (int i = 0; i < ti.num_pieces(); ++i) {
            auto hash = ti.hash_for_piece(i);
            meta.piece_hashes.push_back(lt::aux::to_hex(hash.to_string()));
        }

        return meta;
    } catch (const std::exception& e) {
        std::cerr << "[Parser] Exception parsing torrent file: " << e.what() << std::endl;
        return std::nullopt;
    }
}

std::optional<TorrentMeta> TorrentParser::parse_from_bencoded_data(const std::string& data) {
    try {
        lt::error_code ec;
        lt::bdecode_node bdecode;
        int pos = 0;
        lt::bdecode(&data[0], &data[0] + data.size(), bdecode, ec, &pos);
        if (ec) {
            std::cerr << "[Parser] Failed to decode bencoded data: " << ec.message() << std::endl;
            return std::nullopt;
        }

        lt::torrent_info ti(std::move(bdecode), ec);
        if (ec) {
            std::cerr << "[Parser] Failed to parse decoded data: " << ec.message() << std::endl;
            return std::nullopt;
        }

        TorrentMeta meta;
        meta.infohash = lt::aux::to_hex(ti.info_hash().to_string());
        meta.name = ti.name();
        meta.total_size = ti.total_size();
        meta.piece_length = ti.piece_length();
        meta.piece_count = ti.num_pieces();
        meta.timestamp = std::time(nullptr);

        for (int i = 0; i < ti.num_files(); ++i) {
            auto const& f = ti.file_at(i);
            meta.files.push_back(f.path + " (" + std::to_string(f.size) + " bytes)");
        }

        for (int i = 0; i < ti.num_pieces(); ++i) {
            auto hash = ti.hash_for_piece(i);
            meta.piece_hashes.push_back(lt::aux::to_hex(hash.to_string()));
        }

        return meta;
    } catch (const std::exception& e) {
        std::cerr << "[Parser] Exception parsing bencoded data: " << e.what() << std::endl;
        return std::nullopt;
    }
}

}
