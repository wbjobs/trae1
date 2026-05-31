cmake_minimum_required(VERSION 3.14)
project(fusefs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse3)

add_executable(fusefs
    src/main.cpp
    src/key_manager.cpp
    src/fpe.cpp
    src/crypto.cpp
    src/filesystem.cpp
    src/fuse_ops.cpp
)

target_include_directories(fusefs PRIVATE
    ${FUSE_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(fusefs PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ${FUSE_LIBRARIES}
)

target_compile_options(fusefs PRIVATE
    ${FUSE_CFLAGS_OTHER}
    -Wall -Wextra -Wpedantic
)

install(TARGETS fusefs DESTINATION bin)
