#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
#include <mutex>

#include <openenclave/enclave.h>
#include <openenclave/corelibc/string.h>
#include <openenclave/attestation/attester.h>
#include <openenclave/attestation/sgx/evidence.h>
#include <openenclave/bits/evidence.h>

#include "aggregator_t.h"
#include "enclave_crypto.h"
#include "protocol.h"

namespace sgxagg { namespace encl {

// 线程安全的数据聚合存储
class AggregateStore
{
public:
    void submit(const std::vector<Record>& records) {
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& r : records) {
            incomes_.push_back(r.income);
            ages_.push_back(r.age);
        }
        dirty_ = true;
    }

    AggregateResult compute() {
        std::lock_guard<std::mutex> lk(mtx_);
        AggregateResult r;
        oe_memset(&r, 0, sizeof(r));

        const size_t n = incomes_.size();
        r.total_records = n;
        if (n == 0) return r;

        if (dirty_) {
            sort(incomes_.begin(), incomes_.end());
            sort(ages_.begin(), ages_.end());
            dirty_ = false;
        }

        // mean
        double sum_inc = 0, sum_age = 0;
        for (auto v : incomes_) sum_inc += v;
        for (auto v : ages_)    sum_age += v;
        r.mean_income = sum_inc / n;
        r.mean_age    = sum_age / n;

        // median
        r.median_income = quantile(incomes_, 0.5);
        r.median_age    = quantile(ages_,    0.5);

        // variance
        double var_inc = 0, var_age = 0;
        for (auto v : incomes_) var_inc += (v - r.mean_income) * (v - r.mean_income);
        for (auto v : ages_)    var_age += (v - r.mean_age) * (v - r.mean_age);
        r.variance_income = var_inc / n;
        r.variance_age    = var_age / n;

        // 25/75 quantiles
        r.quantile25_income = quantile(incomes_, 0.25);
        r.quantile75_income = quantile(incomes_, 0.75);

        return r;
    }

private:
    static double quantile(const std::vector<double>& sorted, double q) {
        if (sorted.empty()) return 0.0;
        if (q <= 0) return sorted.front();
        if (q >= 1) return sorted.back();
        double pos = (sorted.size() - 1) * q;
        size_t idx = (size_t)pos;
        double frac = pos - idx;
        if (idx + 1 >= sorted.size()) return sorted.back();
        return sorted[idx] * (1 - frac) + sorted[idx + 1] * frac;
    }

    mutable std::mutex mtx_;
    std::vector<double> incomes_;
    std::vector<double> ages_;
    bool dirty_ = true;
};

static AggregateStore g_store;

// Enclave 全局 ECDH 密钥对（与 quote 绑定的那个）
static EcdhKeyPair g_enclave_kp;
static bool g_kp_generated = false;
static std::mutex g_kp_mutex;

}} // namespace sgxagg::encl

using namespace sgxagg;
using namespace sgxagg::encl;

// ———— ecall 实现 ————

extern "C" OE_EXPORT int ecall_enclave_init(void) {
    std::lock_guard<std::mutex> lk(g_kp_mutex);
    if (!g_kp_generated) {
        if (!ecdh_generate(g_enclave_kp)) return -1;
        g_kp_generated = true;
    }
    return 0;
}

extern "C" int ecall_get_enclave_ecdh_pub(uint8_t pubkey[65]) {
    std::lock_guard<std::mutex> lk(g_kp_mutex);
    if (!g_kp_generated) return -1;
    if (g_enclave_kp.pub.size() != 65) return -1;
    std::memcpy(pubkey, g_enclave_kp.pub.data(), 65);
    return 0;
}

extern "C" int ecall_derive_session_key(
    const uint8_t client_pub[65],
    uint64_t session_id,
    uint8_t derived_key[32]
)
{
    try {
        {
            std::lock_guard<std::mutex> lk(g_kp_mutex);
            if (!g_kp_generated) return -1;
        }

        // Enclave 内派生共享秘密和会话密钥（使用全局 ECDH 密钥对）
        std::vector<uint8_t> shared;
        {
            std::lock_guard<std::mutex> lk(g_kp_mutex);
            shared = ecdh_derive(g_enclave_kp.priv, client_pub);
        }
        std::string info = "aggregator-session-v1|sid=" + std::to_string(session_id);
        std::vector<uint8_t> sk = hkdf_sha256(
            shared.data(), shared.size(),
            (const uint8_t*)info.data(), info.size(),
            kAesGcmKeyLen
        );

        if (sk.size() != 32) return -1;
        std::memcpy(derived_key, sk.data(), 32);
        return 0;
    } catch (...) {
        return -2;
    }
}

extern "C" int ecall_submit_records(
    const uint8_t* plaintext, size_t plain_len
)
{
    try {
        auto records = deserialize_records(plaintext, plain_len);
        if (records.size() > kMaxRecordsPerParty) return -3;
        g_store.submit(records);
        return 0;
    } catch (...) {
        return -1;
    }
}

extern "C" int ecall_get_aggregates(
    uint8_t* out_buf, size_t out_len
)
{
    try {
        if (out_len < sizeof(AggregateResult)) return -1;
        AggregateResult r = g_store.compute();
        std::memcpy(out_buf, &r, sizeof(r));
        return 0;
    } catch (...) {
        return -1;
    }
}

extern "C" int ecall_get_quote(
    uint8_t* quote_buf, size_t quote_buf_size,
    size_t* quote_len
)
{
    oe_result_t rc;
    oe_uuid_t format_id = {OE_FORMAT_UUID_SGX_ECDSA_P256};

    // 报告数据：包含 enclave ECDH 公钥的 SHA256（公钥来自 enclave 内部）
    uint8_t report_data[OE_SHA256_SIZE] = {0};
    {
        std::lock_guard<std::mutex> lk(g_kp_mutex);
        if (!g_kp_generated) return -1;
        if (oe_sha256(g_enclave_kp.pub.data(), 65, report_data) != OE_OK)
            return -1;
    }

    size_t ql = 0;
    rc = oe_get_evidence(
        &format_id, OE_EVIDENCE_FLAGS_EMBED_FORMAT_ID,
        report_data, sizeof(report_data),
        nullptr, 0,
        quote_buf, quote_buf_size, &ql,
        nullptr, 0
    );
    if (rc != OE_OK) return -2;
    *quote_len = ql;
    return 0;
}
