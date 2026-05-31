# Enclave 构建
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/aggregator_t.h
           ${CMAKE_CURRENT_BINARY_DIR}/aggregator_t.c
           ${CMAKE_CURRENT_BINARY_DIR}/aggregator_u.h
           ${CMAKE_CURRENT_BINARY_DIR}/aggregator_u.c
    COMMAND openenclave::oeedger8r ${CMAKE_CURRENT_SOURCE_DIR}/aggregator.edl
            --search-path ${OE_INCLUDEDIR}
            --trusted-dir ${CMAKE_CURRENT_BINARY_DIR}
            --untrusted-dir ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/aggregator.edl
    COMMENT "Generating EDL bindings for aggregator enclave"
)

# ========== Enclave 本体 ==========
add_executable(aggregator_enclave
    enclave.cpp
    enclave_crypto.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/aggregator_t.c
)

target_compile_options(aggregator_enclave PRIVATE -nostdinc -fno-stack-protector)
target_include_directories(aggregator_enclave PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/shared
)
target_link_libraries(aggregator_enclave PRIVATE
    openenclave::oeenclave
    openenclave::oelibc
    openenclave::oelibcxx
    openenclave::oehostepoll
    openenclave::oecryptoopenssl
    openenclave::oeattestation
)

set_target_properties(aggregator_enclave PROPERTIES
    SUFFIX ".so"
    PREFIX ""
)

# 签名 enclave：使用私钥签名，生成可加载的 .signed.so
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/aggregator_enclave.signed.so
    COMMAND openenclave::oesign sign
            -e ${CMAKE_CURRENT_BINARY_DIR}/aggregator_enclave.so
            -c ${CMAKE_CURRENT_SOURCE_DIR}/enclave.conf
            -k ${CMAKE_SOURCE_DIR}/signing/enclave_key.pem
    DEPENDS aggregator_enclave
            ${CMAKE_CURRENT_SOURCE_DIR}/enclave.conf
            ${CMAKE_SOURCE_DIR}/signing/enclave_key.pem
    COMMENT "Signing aggregator enclave"
)
add_custom_target(sign_enclave ALL DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/aggregator_enclave.signed.so)

# 同时把未签名的 enclave 导出给 host（用于创建 enclave）
set(ENCLAVE_SIGNED_PATH
    ${CMAKE_CURRENT_BINARY_DIR}/aggregator_enclave.signed.so
    PARENT_SCOPE)
set(ENCLAVE_EDL_UNTRUSTED_SRC
    ${CMAKE_CURRENT_BINARY_DIR}/aggregator_u.c
    PARENT_SCOPE)
set(ENCLAVE_EDL_BINARY_DIR
    ${CMAKE_CURRENT_BINARY_DIR}
    PARENT_SCOPE)
