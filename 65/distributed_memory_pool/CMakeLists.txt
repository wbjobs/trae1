cmake_minimum_required(VERSION 3.14)
project(DistributedMemoryPool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS "Build test executables" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(IBVERBS REQUIRED libibverbs)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

set(PROTO_DIR ${CMAKE_SOURCE_DIR}/proto)
set(PROTO_FILE ${PROTO_DIR}/memory_service.proto)

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(PROTO_SRCS
    ${GENERATED_DIR}/memory_service.pb.cc
    ${GENERATED_DIR}/memory_service.grpc.pb.cc
)

add_custom_command(
    OUTPUT
        ${GENERATED_DIR}/memory_service.pb.cc
        ${GENERATED_DIR}/memory_service.pb.h
        ${GENERATED_DIR}/memory_service.grpc.pb.cc
        ${GENERATED_DIR}/memory_service.grpc.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS
        --grpc_out=${GENERATED_DIR}
        --cpp_out=${GENERATED_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        -I ${PROTO_DIR}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating gRPC/Protobuf code from memory_service.proto"
)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${GENERATED_DIR}
    ${IBVERBS_INCLUDE_DIRS}
    ${GRPC_INCLUDE_DIRS}
    ${PROTOBUF_INCLUDE_DIRS}
)

link_directories(
    ${IBVERBS_LIBRARY_DIRS}
    ${GRPC_LIBRARY_DIRS}
    ${PROTOBUF_LIBRARY_DIRS}
)

set(COMMON_SOURCES
    src/memory/slab_allocator.cpp
    src/memory/memory_pool.cpp
    src/memory/distributed_lock_manager.cpp
    src/rdma/rdma_context.cpp
    src/rdma/rdma_endpoint.cpp
    src/rdma/rdma_transport.cpp
    src/service/memory_service_impl.cpp
    src/monitor/monitor.cpp
    src/monitor/prometheus_exporter.cpp
    src/rpc/server.cpp
)

set(HEADERS
    include/common/types.h
    include/common/config.h
    include/common/utils.h
    include/memory/slab_allocator.h
    include/memory/memory_pool.h
    include/memory/distributed_lock_manager.h
    include/rdma/rdma_context.h
    include/rdma/rdma_endpoint.h
    include/rdma/rdma_transport.h
    include/service/memory_service_impl.h
    include/monitor/monitor.h
    include/monitor/prometheus_exporter.h
)

add_library(dmp_core STATIC
    ${COMMON_SOURCES}
    ${PROTO_SRCS}
    ${HEADERS}
)

target_include_directories(dmp_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${GENERATED_DIR}
)

target_link_libraries(dmp_core PUBLIC
    ${IBVERBS_LIBRARIES}
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    grpc++_reflection
    pthread
    dl
)

add_executable(dmp_server
    src/main_server.cpp
)

target_link_libraries(dmp_server PRIVATE dmp_core)

add_executable(dmp_client
    client/client_main.cpp
)

target_link_libraries(dmp_client PRIVATE dmp_core)

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include_directories(${GTEST_INCLUDE_DIRS})

    add_executable(dmp_tests
        tests/test_main.cpp
        tests/test_slab_allocator.cpp
        tests/test_memory_pool.cpp
    )

    target_link_libraries(dmp_tests PRIVATE
        dmp_core
        ${GTEST_LIBRARIES}
        pthread
    )

    add_test(NAME DMPTests COMMAND dmp_tests)
endif()

install(TARGETS dmp_server dmp_client DESTINATION bin)
install(DIRECTORY include/ DESTINATION include/dmp)
