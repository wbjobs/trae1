#include "enclave_host.h"
#include <iostream>
#include <cstring>
#include <stdexcept>

#include "aggregator_u.h"

namespace sgxagg { namespace host {

EnclaveHost& EnclaveHost::instance() {
    static EnclaveHost s_inst;
    return s_inst;
}

bool EnclaveHost::create(const std::string& enclave_signed_path) {
    if (created_) return true;

    uint32_t flags = OE_ENCLAVE_FLAG_DEBUG | OE_ENCLAVE_FLAG_SIMULATE;
    oe_enclave_t* enc = nullptr;
    oe_result_t rc = oe_create_aggregator_enclave(
        enclave_signed_path.c_str(),
        OE_ENCLAVE_TYPE_SGX,
        flags,
        nullptr, 0,
        &enc
    );
    if (rc != OE_OK) {
        std::cerr << "oe_create_enclave failed: " << oe_result_str(rc) << std::endl;
        return false;
    }

    enclave_ = enc;
    created_ = true;

    // 初始化 enclave（内部生成 ECDH 密钥对）
    int ret = 0;
    rc = ecall_enclave_init(enclave_, &ret);
    if (rc != OE_OK || ret != 0) {
        std::cerr << "ecall_enclave_init failed" << std::endl;
        destroy();
        return false;
    }
    return true;
}

void EnclaveHost::destroy() {
    if (enclave_) {
        oe_terminate_enclave(enclave_);
        enclave_ = nullptr;
    }
    created_ = false;
}

bool EnclaveHost::get_quote(std::vector<uint8_t>& out_quote) {
    if (!enclave_) return false;
    out_quote.resize(16384);
    size_t quote_len = 0;
    int ret = 0;
    oe_result_t rc = ecall_get_quote(
        enclave_, &ret,
        out_quote.data(), out_quote.size(), &quote_len
    );
    if (rc != OE_OK || ret != 0) return false;
    out_quote.resize(quote_len);
    return true;
}

bool EnclaveHost::get_enclave_ecdh_pub(uint8_t out_pub[65]) {
    if (!enclave_) return false;
    int ret = 0;
    oe_result_t rc = ecall_get_enclave_ecdh_pub(enclave_, &ret, out_pub);
    return (rc == OE_OK && ret == 0);
}

bool EnclaveHost::derive_session_key(const uint8_t client_pub[65],
                                     uint64_t session_id,
                                     uint8_t out_key[32]) {
    if (!enclave_) return false;
    int ret = 0;
    oe_result_t rc = ecall_derive_session_key(enclave_, &ret, client_pub, session_id, out_key);
    return (rc == OE_OK && ret == 0);
}

bool EnclaveHost::submit_records(const uint8_t* plain, size_t len) {
    if (!enclave_) return false;
    int ret = 0;
    oe_result_t rc = ecall_submit_records(enclave_, &ret, plain, len);
    return (rc == OE_OK && ret == 0);
}

bool EnclaveHost::get_aggregates(AggregateResult& out) {
    if (!enclave_) return false;
    uint8_t buf[sizeof(AggregateResult)];
    int ret = 0;
    oe_result_t rc = ecall_get_aggregates(enclave_, &ret, buf, sizeof(buf));
    if (rc != OE_OK || ret != 0) return false;
    std::memcpy(&out, buf, sizeof(out));
    return true;
}

}} // sgxagg::host
