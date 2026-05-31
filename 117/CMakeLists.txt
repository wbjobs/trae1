cmake_minimum_required(VERSION 3.15)
project(mosh_plus_plus VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/include)

set(COMMON_SOURCES
    src/common/udp_transport.cpp
    src/common/crypto.cpp
    src/common/protocol.cpp
    src/common/session.cpp
    src/common/utils.cpp
    src/common/circular_buffer.cpp
    src/common/snapshot.cpp
    src/common/auth.cpp
    src/common/redis_store.cpp
    src/common/crdt.cpp
)

set(CLIENT_SOURCES
    src/client/main.cpp
    src/client/mosh_client.cpp
    src/client/terminal.cpp
)

set(SERVER_SOURCES
    src/server/main.cpp
    src/server/mosh_server.cpp
    src/server/session_manager.cpp
    src/server/pty.cpp
)

add_library(mosh_common STATIC ${COMMON_SOURCES})
target_link_libraries(mosh_common
    ${OPENSSL_LIBRARIES}
    Threads::Threads
)

add_executable(mosh-client ${CLIENT_SOURCES})
target_link_libraries(mosh-client
    mosh_common
    ${OPENSSL_LIBRARIES}
    Threads::Threads
)

add_executable(mosh-server ${SERVER_SOURCES})
target_link_libraries(mosh-server
    mosh_common
    ${OPENSSL_LIBRARIES}
    Threads::Threads
)

install(TARGETS mosh-client mosh-server
    RUNTIME DESTINATION bin
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(mosh_common PRIVATE -Wall -Wextra -g)
    target_compile_options(mosh-client PRIVATE -Wall -Wextra -g)
    target_compile_options(mosh-server PRIVATE -Wall -Wextra -g)
else()
    target_compile_options(mosh_common PRIVATE -O3 -Wall -Wextra)
    target_compile_options(mosh-client PRIVATE -O3 -Wall -Wextra)
    target_compile_options(mosh-server PRIVATE -O3 -Wall -Wextra)
endif()
