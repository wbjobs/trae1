cmake_minimum_required(VERSION 3.16)
project(nvmeof_target C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -flto")

if(NOT SPDK_ROOT_DIR)
    set(SPDK_ROOT_DIR $ENV{SPDK_ROOT_DIR})
endif()
if(NOT SPDK_ROOT_DIR)
    message(FATAL_ERROR "Please set SPDK_ROOT_DIR (e.g. -DSPDK_ROOT_DIR=/opt/spdk)")
endif()

set(SPDK_INCLUDE_DIRS
    ${SPDK_ROOT_DIR}/include
    ${SPDK_ROOT_DIR}/include/spdk
)

find_path(SPDK_ENV_HEADER spdk/env.h
    PATHS ${SPDK_ROOT_DIR}/include NO_DEFAULT_PATH)
if(NOT SPDK_ENV_HEADER)
    message(FATAL_ERROR "SPDK headers not found under ${SPDK_ROOT_DIR}/include")
endif()

set(SPDK_LIB_DIRS
    ${SPDK_ROOT_DIR}/lib
    ${SPDK_ROOT_DIR}/build/lib
)
link_directories(${SPDK_LIB_DIRS})

set(SPDK_LIBS
    spdk_nvme
    spdk_nvmf
    spdk_bdev
    spdk_bdev_nvme
    spdk_bdev_aio
    spdk_bdev_null
    spdk_bdev_malloc
    spdk_jsonrpc
    spdk_json
    spdk_rpc
    spdk_log
    spdk_env_dpdk
    spdk_sock
    spdk_sock_posix
    spdk_trace
    spdk_util
    spdk_event
    spdk_thread
    spdk_conf
    spdk_dma
    spdk_keyring
    spdk_notify
    numa
    uuid
    dl
    rt
    pthread
    crypto
    aio
)

include_directories(${SPDK_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/src)

add_executable(nvmeof_target
    src/nvmeof_target.c
    src/config_parser.c
    src/nvmeof_rpc.c
    src/nvmeof_lock.c
    src/bdev_lock.c
    src/nvmeof_repl.c
    src/bdev_repl.c
)
target_link_libraries(nvmeof_target ${SPDK_LIBS})

add_executable(nvmeof_cli
    src/nvmeof_cli.c
)
target_link_libraries(nvmeof_cli
    spdk_jsonrpc
    spdk_json
    spdk_log
    spdk_env_dpdk
    spdk_util
    pthread
    dl
    rt
    numa
    uuid
)
