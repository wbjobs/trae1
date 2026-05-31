#include "crypto.h"
#include <cstring>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/hkdf.h>

namespace sgxagg { namespace crypto {

static void openssl_check(int rc, const char* what) {
    if (rc <= 0) {
        unsigned long e = ERR_get_error();
        char buf[256] = {0};
        ERR_error_string_n(e, buf, sizeof(buf));
        throw std::runtime_error(std::string(what) + ": " + buf);
    }
}

bool ecdh_generate(EcdhKeyPair& kp) {
    try {
        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
        if (!pctx) return false;
        if (EVP_PKEY_keygen_init(pctx) <= 0) { EVP_PKEY_CTX_free(pctx); return false; }
        if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1) <= 0) {
            EVP_PKEY_CTX_free(pctx); return false;
        }
        EVP_PKEY* pkey = nullptr;
        if (EVP_PKEY_keygen(pctx, &pkey) <= 0) { EVP_PKEY_CTX_free(pctx); return false; }
        EVP_PKEY_CTX_free(pctx);

        // 私钥 DER
        int len = i2d_PrivateKey(pkey, nullptr);
        if (len <= 0) { EVP_PKEY_free(pkey); return false; }
        kp.priv.resize(len);
        unsigned char* p = kp.priv.data();
        i2d_PrivateKey(pkey, &p);

        // 公钥：未压缩 65 字节
        EC_KEY* ec = EVP_PKEY_get0_EC_KEY(pkey);
        const EC_POINT* pub = EC_KEY_get0_public_key(ec);
        const EC_GROUP* group = EC_KEY_get0_group(ec);
        kp.pub.resize(65);
        len = EC_POINT_point2oct(group, pub, POINT_CONVERSION_UNCOMPRESSED,
                                  kp.pub.data(), kp.pub.size(), nullptr);
        EVP_PKEY_free(pkey);
        if (len != 65) return false;
        return true;
    } catch (...) { return false; }
}

std::vector<uint8_t> ecdh_derive(const std::vector<uint8_t>& our_priv_der,
                                 const uint8_t* their_pub, size_t their_pub_len) {
    const unsigned char* p = our_priv_der.data();
    EVP_PKEY* our = d2i_PrivateKey(EVP_PKEY_EC, nullptr, &p, (long)our_priv_der.size());
    if (!our) throw std::runtime_error("d2i_PrivateKey failed");

    EC_KEY* ecpub = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    EC_POINT* pt = EC_POINT_new(group);
    if (!EC_POINT_oct2point(group, pt, their_pub, their_pub_len, nullptr)) {
        EC_POINT_free(pt); EC_GROUP_free(group); EC_KEY_free(ecpub); EVP_PKEY_free(our);
        throw std::runtime_error("EC_POINT_oct2point failed");
    }
    EC_KEY_set_public_key(ecpub, pt);
    EVP_PKEY* their = EVP_PKEY_new();
    EVP_PKEY_assign_EC_KEY(their, ecpub);
    EC_POINT_free(pt);
    EC_GROUP_free(group);

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(our, nullptr);
    openssl_check(EVP_PKEY_derive_init(ctx), "EVP_PKEY_derive_init");
    openssl_check(EVP_PKEY_derive_set_peer(ctx, their), "EVP_PKEY_derive_set_peer");

    size_t secret_len = 0;
    openssl_check(EVP_PKEY_derive(ctx, nullptr, &secret_len), "EVP_PKEY_derive(len)");
    std::vector<uint8_t> secret(secret_len);
    openssl_check(EVP_PKEY_derive(ctx, secret.data(), &secret_len), "EVP_PKEY_derive");

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(our);
    EVP_PKEY_free(their);
    return secret;
}

std::vector<uint8_t> hkdf_sha256(const uint8_t* ikm, size_t ikm_len,
                                 const uint8_t* info, size_t info_len,
                                 size_t out_len) {
    std::vector<uint8_t> out(out_len);
    openssl_check(HKDF(out.data(), out.size(), EVP_sha256(),
                       ikm, ikm_len,
                       (const uint8_t*)"", 0,
                       info, info_len), "HKDF");
    return out;
}

std::vector<uint8_t> derive_session_key(const std::vector<uint8_t>& shared,
                                        uint64_t session_id) {
    std::string info = "aggregator-session-v1|sid=" + std::to_string(session_id);
    return hkdf_sha256(shared.data(), shared.size(),
                       (const uint8_t*)info.data(), info.size(),
                       kAesGcmKeyLen);
}

bool random_bytes(uint8_t* buf, size_t n) {
    return RAND_bytes(buf, (int)n) == 1;
}

std::vector<uint8_t> aes_gcm_encrypt(const uint8_t* key, const uint8_t* plaintext, size_t pt_len,
                                     const uint8_t* aad, size_t aad_len) {
    uint8_t nonce[kAesGcmNonceLen];
    if (!random_bytes(nonce, sizeof(nonce))) throw std::runtime_error("random_bytes failed");

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)) {
        EVP_CIPHER_CTX_free(ctx); throw std::runtime_error("EVP_EncryptInit_ex failed");
    }
    if (1 != EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce)) {
        EVP_CIPHER_CTX_free(ctx); throw std::runtime_error("EVP_EncryptInit_ex(key/nonce) failed");
    }
    int outl = 0;
    if (aad_len > 0) {
        if (1 != EVP_EncryptUpdate(ctx, nullptr, &outl, aad, (int)aad_len)) {
            EVP_CIPHER_CTX_free(ctx); throw std::runtime_error("EVP_EncryptUpdate(aad) failed");
        }
    }
    std::vector<uint8_t> out(sizeof(nonce) + pt_len + kAesGcmTagLen);
    size_t off = sizeof(nonce);
    std::memcpy(out.data(), nonce, sizeof(nonce));
    if (pt_len > 0) {
        if (1 != EVP_EncryptUpdate(ctx, out.data() + off, &outl, plaintext, (int)pt_len)) {
            EVP_CIPHER_CTX_free(ctx); throw std::runtime_error("EVP_EncryptUpdate failed");
        }
    }
    if (1 != EVP_EncryptFinal_ex(ctx, out.data() + off + outl, &outl)) {
        EVP_CIPHER_CTX_free(ctx); throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }
    uint8_t tag[kAesGcmTagLen];
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kAesGcmTagLen, tag)) {
        EVP_CIPHER_CTX_free(ctx); throw std::runtime_error("EVP_CTRL_GCM_GET_TAG failed");
    }
    EVP_CIPHER_CTX_free(ctx);

    std::memcpy(out.data() + sizeof(nonce) + pt_len, tag, kAesGcmTagLen);
    out.resize(sizeof(nonce) + pt_len + kAesGcmTagLen);
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

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    if (1 != EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce)) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    int outl = 0;
    if (aad_len > 0) {
        if (1 != EVP_DecryptUpdate(ctx, nullptr, &outl, aad, (int)aad_len)) {
            EVP_CIPHER_CTX_free(ctx); return false;
        }
    }
    out.resize(ct_len);
    if (ct_len > 0) {
        if (1 != EVP_DecryptUpdate(ctx, out.data(), &outl, ct, (int)ct_len)) {
            EVP_CIPHER_CTX_free(ctx); return false;
        }
    }
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kAesGcmTagLen, (void*)tag)) {
        EVP_CIPHER_CTX_free(ctx); return false;
    }
    int rc = EVP_DecryptFinal_ex(ctx, out.data() + outl, &outl);
    EVP_CIPHER_CTX_free(ctx);
    if (rc != 1) return false;
    out.resize(ct_len);
    return true;
}

}} // sgxagg::crypto
