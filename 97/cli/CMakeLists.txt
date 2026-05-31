# CLI 工具：enclave 签名 / quote 验证 / 密钥生成
add_executable(aggregator_cli
    cli.cpp
)

target_include_directories(aggregator_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(aggregator_cli PRIVATE
    sgx_aggregator_shared
    openenclave::oehost
    openenclave::oehostepoll
    pthread
)
