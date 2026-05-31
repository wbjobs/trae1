cmake_minimum_required(VERSION 3.15)
project(sgx_aggregator LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED ENV{OE_SDK_PATH})
    message(FATAL_ERROR "请设置环境变量 OE_SDK_PATH 指向 Open Enclave SDK 安装目录")
endif()

list(APPEND CMAKE_MODULE_PATH "$ENV{OE_SDK_PATH}/share/openenclave/cmake")
find_package(OpenEnclave CONFIG REQUIRED)

add_subdirectory(shared)
add_subdirectory(enclave)
add_subdirectory(host)
add_subdirectory(client)
add_subdirectory(cli)
