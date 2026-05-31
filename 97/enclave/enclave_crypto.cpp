#include "enclave_crypto.h"
#include <cstring>
#include <stdexcept>
#include <vector>

#include <openenclave/enclave.h>
#include <openenclave/corelibc/string.h>
#include <openenclave/internal/crypto/ec.h>
#include <openenclave/internal/crypto/hkdf.h>
#include <openenclave/internal/crypto/evp.h>
#include <openenclave/internal/random.h>

#include "aggregator_t.h"

namespace sgxagg { namespace encl {

bool random_bytes(uint8_t* buf, size_t n) {
    return oe_random_internal(buf, n) == OE_OK;
}

bool ecdh_generate(EcdhKeyPair& kp) {
    oe_ec_t* ctx = nullptr;
    if (oe_ec_create( OE_EC_SECP256R1, &ctx) != OE_OK) return false;
    if (oe_ec_generate_key_pair(ctx) != OE_OK) { oe_ec_free(ctx); return false; }

    // 公钥
    uint8_t pub[65]; size_t pub_len = sizeof(pub);
    if (oe_ec_public_key_from_handle(ctx, pub, &pub_len) != OE_OK || pub_len != 65) {
        oe_ec_free(ctx); return false;
    }
    kp.pub.assign(pub, pub + pub_len);

    // 私钥：raw 32 字节
    uint8_t priv[32]; size_t priv_len = sizeof(priv);
    if (oe_ec_private_key_from_handle(ctx, priv, &priv_len) != OE_OK || priv_len != 32) {
        oe_ec_free(ctx); return false;
    }
    kp.priv.assign(priv, priv + priv_len);

    oe_ec_free(ctx);
    return true;
}

std::vector<uint8_t> ecdh_derive(const std::vector<uint8_t>& priv_raw,
                                 const uint8_t* their_pub) {
    oe_ec_t* ctx = nullptr;
    if (oe_ec_create(OE_EC_SECP256R1, &ctx) != OE_OK)
        throw std::runtime_error("oe_ec_create failed");

    // 导入私钥
    if (oe_ec_import_private_key(ctx, OE_EC_SECP256R1,
                                 priv_raw.data(), priv_raw.size()) != OE_OK) {
        oe_ec_free(ctx); throw std::runtime_error("oe_ec_import_private_key failed");
    }

    // 导入远端公钥
    oe_ec_public_key_t pub;
    oe_memset(&pub, 0, sizeof(pub));
    pub.type = OE_EC_SECP256R1;
    oe_memcpy(pub.data, their_pub, 65);

    uint8_t secret[32]; size_t secret_len = sizeof(secret);
    oe_result_t rc = oe_ec_compute_shared_secret(ctx, &pub, secret, &secret_len);
    oe_ec_free(ctx);
    if (rc != OE_OK) throw std::runtime_error("oe_ec_compute_shared_secret failed");
    return std::vector<uint8_t>(secret, secret + secret_len);
}

std::vector<uint8_t> hkdf_sha256(const uint8_t* ikm, size_t ikm_len,
                                 const uint8_t* info, size_t info_len,
                                 size_t out_len) {
    std::vector<uint8_t> out(out_len);
    if (oe_hkdf_sha256(out.data(), out.size(),
                      ikm, ikm_len,
                      nullptr, 0,
                      info, info_len) != OE_OK)
        throw std::runtime_error("oe_hkdf_sha256 failed");
    return out;
}

bool aes_gcm_decrypt(const uint8_t* key, const uint8_t* input, size_t in_len,
                     std::vector<uint8_t>& out,
                     const uint8_t* aad, size_t aad_len) {
    if (in_len < kAesGcmNonceLen + kAesGcmTagLen) return false;
    const uint8_t* nonce = input;
    size_t ct_len = in_len - kAesGcmNonceLen - kAesGcmTagLen;
    const uint8_t* ct = input + kAesGcmNonceLen;
    const uint8_t* tag = input + kAesGcmNonceLen + ct_len;
    out.resize(ct_len);

    size_t outl = 0;
    oe_result_t rc = oe_aes_gcm_decrypt(
        key, kAesGcmKeyLen * 8,
        nonce, kAesGcmNonceLen,
        aad, aad_len,
        ct, ct_len,
        tag, kAesGcmTagLen,
        out.data(), out.size(), &outl
    );
    if (rc != OE_OK) return false;
    out.resize(outl);
    return true;
}

std::vector<uint8_t> aes_gcm_encrypt(const uint8_t* key,
                                     const uint8_t* plaintext, size_t pt_len,
                                     const uint8_t* aad, size_t aad_len) {
    uint8_t nonce[kAesGcmNonceLen];
    if (!random_bytes(nonce, sizeof(nonce)))
        throw std::runtime_error("enclave random_bytes failed");

    std::vector<uint8_t> out(kAesGcmNonceLen + pt_len + kAesGcmTagLen);
    std::memcpy(out.data(), nonce, kAesGcmNonceLen);

    uint8_t* ct = out.data() + kAesGcmNonceLen;
    uint8_t* tag = out.data() + kAesGcmNonceLen + pt_len;
    size_t ct_len = 0;

    oe_result_t rc = oe_aes_gcm_encrypt(
        key, kAesGcmKeyLen * 8,
        nonce, kAesGcmNonceLen,
        aad, aad_len,
        plaintext, pt_len,
        ct, pt_len, &ct_len,
        tag, kAesGcmTagLen
    );
    if (rc != OE_OK) throw std::runtime_error("oe_aes_gcm_encrypt failed");
    out.resize(kAesGcmNonceLen + ct_len + kAesGcmTagLen);
    return out;
}

}} // sgxagg::encl

// OCALL 代理：给 enclave 提供安全随机数
void ocall_get_random(uint8_t* buf, size_t len) {
    oe_get_random(buf, len);
}
