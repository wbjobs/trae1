# Host 构建
add_executable(aggregator_host
    main.cpp
    enclave_host.cpp
    session_manager.cpp
    server.cpp
    ${ENCLAVE_EDL_UNTRUSTED_SRC}
)

target_include_directories(aggregator_host PRIVATE
    ${ENCLAVE_EDL_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(aggregator_host PRIVATE
    sgx_aggregator_shared
    openenclave::oehost
    openenclave::oehostepoll
    openenclave::oehostfs
    pthread
)

add_dependencies(aggregator_host sign_enclave)

target_compile_definitions(aggregator_host PRIVATE
    ENCLAVE_SIGNED_PATH=\"${ENCLAVE_SIGNED_PATH}\"
)
