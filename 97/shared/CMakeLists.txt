# 公共数据结构 —— 被 enclave / host / client / cli 共享
set(SHARED_SOURCES
    protocol.cpp
    crypto.cpp
)

add_library(sgx_aggregator_shared STATIC ${SHARED_SOURCES})
target_include_directories(sgx_aggregator_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(OpenSSL REQUIRED)
target_link_libraries(sgx_aggregator_shared PUBLIC OpenSSL::SSL OpenSSL::Crypto)
