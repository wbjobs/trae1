# 客户端 Demo
add_executable(aggregator_client
    client.cpp
)

target_include_directories(aggregator_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(aggregator_client PRIVATE
    sgx_aggregator_shared
    pthread
)
