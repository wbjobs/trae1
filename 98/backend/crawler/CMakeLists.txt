cmake_minimum_required(VERSION 3.16)
project(bt_monitor_crawler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

find_path(LIBTORRENT_INCLUDE_DIR libtorrent/session.hpp)
find_library(LIBTORRENT_LIBRARY NAMES torrent-rasterbar torrent)

if(NOT LIBTORRENT_INCLUDE_DIR OR NOT LIBTORRENT_LIBRARY)
    message(FATAL_ERROR "libtorrent not found. Install libtorrent-rasterbar-dev or libtorrent-rasterbar")
endif()

find_path(CPR_INCLUDE_DIR cpr/cpr.h)
find_library(CPR_LIBRARY NAMES cpr)

if(NOT CPR_INCLUDE_DIR OR NOT CPR_LIBRARY)
    message(FATAL_ERROR "cpr library not found. Install libcpr-dev or cpr")
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${LIBTORRENT_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${CPR_INCLUDE_DIR}
)

set(SOURCES
    src/main.cpp
    src/dht_crawler.cpp
    src/torrent_parser.cpp
)

add_executable(bt_crawler ${SOURCES})

target_link_libraries(bt_crawler
    ${LIBTORRENT_LIBRARY}
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${CPR_LIBRARY}
    Threads::Threads
)

if(MSVC)
    target_compile_options(bt_crawler PRIVATE /W4)
else()
    target_compile_options(bt_crawler PRIVATE -Wall -Wextra)
endif()

install(TARGETS bt_crawler DESTINATION bin)
