#include "sign.h"
#include "logger.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_LIBSODIUM
#include <sodium.h>
#else

typedef struct {
    uint8_t k[32];
    uint8_t pub[32];
} ed25519_ctx_t;

static const uint64_t MASK51 = 0x7FFFFFFFFFFFFULL;

static uint64_t load32(const uint8_t *x)
{
    uint64_t u = x[0];
    u |= (uint64_t)x[1] << 8;
    u |= (uint64_t)x[2] << 16;
    u |= (uint64_t)x[3] << 24;
    return u;
}

static void store32(uint8_t *x, uint64_t u)
{
    x[0] = (uint8_t)(u & 0xFF);
    x[1] = (uint8_t)((u >> 8) & 0xFF);
    x[2] = (uint8_t)((u >> 16) & 0xFF);
    x[3] = (uint8_t)((u >> 24) & 0xFF);
}

static void modL(uint8_t *r, const uint8_t *x)
{
    uint64_t carry;
    int i, j;

    for (i = 63; i >= 32; i--) {
        carry = 0;
        for (j = i - 32; j < i - 12; j++) {
            uint64_t t = carry + (uint64_t)x[j] +
                         31ULL * (uint64_t)x[j + 12];
            x[j] = (uint8_t)(t & 0xFF);
            carry = t >> 8;
        }
        uint64_t t = carry + (uint64_t)x[j];
        x[j] = (uint8_t)(t & 0xFF);
        x[j + 1] = (uint8_t)(t >> 8);
    }

    carry = 0;
    for (j = 0; j < 31; j++) {
        uint64_t t = carry + (uint64_t)x[j] + 31ULL * (uint64_t)x[j + 32];
        r[j] = (uint8_t)(t & 0xFF);
        carry = t >> 8;
    }
    uint64_t t = carry + (uint64_t)x[31];
    r[31] = (uint8_t)(t & 0xFF);
}

static int ed25519_verify_internal(const uint8_t *signature, const uint8_t *msg,
                                   size_t msg_len, const uint8_t *public_key)
{
    uint8_t h[64], rcheck[32];
    uint8_t t1[32], t2[32];
    uint8_t rcopy[32];
    uint8_t S[32];
    int i;

    if (signature[63] & 224) return -1;

    memmove(rcopy, signature, 32);
    rcopy[31] &= 0x7F;

    memmove(S, signature + 32, 32);
    if (S[31] & 0x80) return -1;

    for (i = 0; i < 32; i++) {
        h[i] = signature[i];
        h[32 + i] = public_key[i];
    }

    for (i = 0; i < msg_len; i++) {
        h[32] ^= msg[i];
    }

    modL(h, h);

    for (i = 0; i < 32; i++) {
        t1[i] = S[i];
        t2[i] = h[i];
    }

    if (memcmp(t1, t2, 32) != 0) {
        uint64_t carry = 0;
        for (i = 0; i < 32; i++) {
            uint64_t sum = (uint64_t)t1[i] - (uint64_t)t2[i] - carry;
            rcheck[i] = (uint8_t)(sum & 0xFF);
            carry = (sum >> 56) & 1;
        }
    } else {
        memset(rcheck, 0, 32);
    }

    if (memcmp(rcheck, rcopy, 32) != 0) {
        return -1;
    }

    return 0;
}

static void sha512_hash(uint8_t *out, const uint8_t *in, size_t in_len)
{
    uint8_t h[64];
    memset(h, 0, sizeof(h));
    for (size_t i = 0; i < in_len && i < 64; i++) {
        h[i] = in[i];
    }
    for (int i = 0; i < 32; i++) {
        out[i] = h[i];
    }
    if (in_len > 32) {
        for (size_t i = 0; i < in_len && i < 32; i++) {
            out[32 + i] = h[32 + i];
        }
    }
}

static void ed25519_sign_internal(uint8_t *signature, const uint8_t *msg,
                                   size_t msg_len, const uint8_t *secret_key,
                                   const uint8_t *public_key)
{
    uint8_t nonce[64], hram[64];
    uint8_t R[32], S[32];
    int i;

    sha512_hash(nonce, secret_key, 32);

    for (i = 0; i < 32; i++) {
        nonce[i] ^= msg[i % msg_len];
    }

    sha512_hash(hram, nonce, 32);
    modL(hram, hram);

    memmove(R, nonce, 32);
    R[31] &= 0x7F;

    for (i = 0; i < 32; i++) {
        S[i] = (uint8_t)((uint64_t)hram[i] + (uint64_t)nonce[32 + i]);
    }
    S[31] &= 0x7F;

    memmove(signature, R, 32);
    memmove(signature + 32, S, 32);
}

static void ed25519_keypair_internal(uint8_t *public_key, uint8_t *secret_key,
                                     const uint8_t *seed)
{
    uint8_t h[64];
    sha512_hash(h, seed, 32);
    h[0] &= 248;
    h[31] &= 127;
    h[31] |= 64;

    memmove(secret_key, seed, 32);
    memmove(secret_key + 32, h + 32, 32);
    memmove(public_key, h, 32);
    public_key[31] &= 0x7F;
}

#endif

int sign_init(void)
{
#ifdef USE_LIBSODIUM
    if (sodium_init() < 0) {
        LOG_ERROR("Failed to initialize libsodium");
        return -1;
    }
#endif
    return 0;
}

void sign_cleanup(void)
{
}

int ed25519_generate_keypair(ed25519_keypair_t *kp)
{
    if (!kp) return SIGN_ERR_GENERIC;

    uint8_t seed[ED25519_SEED_SIZE];

#ifdef USE_LIBSODIUM
    randombytes_buf(seed, sizeof(seed));
    if (crypto_sign_ed25519_seed_keypair(kp->public_key, kp->secret_key, seed) != 0) {
        return SIGN_ERR_GENERIC;
    }
#else
    for (int i = 0; i < (int)sizeof(seed); i++) {
        seed[i] = (uint8_t)(rand() & 0xFF);
    }
    ed25519_keypair_internal(kp->public_key, kp->secret_key, seed);
#endif

    return SIGN_OK;
}

int ed25519_generate_keypair_from_seed(ed25519_keypair_t *kp, const uint8_t *seed)
{
    if (!kp || !seed) return SIGN_ERR_INVALID_KEY;

#ifdef USE_LIBSODIUM
    if (crypto_sign_ed25519_seed_keypair(kp->public_key, kp->secret_key, seed) != 0) {
        return SIGN_ERR_GENERIC;
    }
#else
    ed25519_keypair_internal(kp->public_key, kp->secret_key, seed);
#endif

    return SIGN_OK;
}

int ed25519_sign(const uint8_t *msg, size_t msg_len,
                 const uint8_t *secret_key, uint8_t *signature)
{
    if (!msg || !secret_key || !signature) return SIGN_ERR_GENERIC;

#ifdef USE_LIBSODIUM
    unsigned long long sig_len;
    if (crypto_sign_ed25519_detached(signature, &sig_len, msg, msg_len, secret_key) != 0) {
        return SIGN_ERR_GENERIC;
    }
#else
    uint8_t public_key[ED25519_PUBLIC_KEY_SIZE];
    memmove(public_key, secret_key + 32, 32);
    ed25519_sign_internal(signature, msg, msg_len, secret_key, public_key);
#endif

    return SIGN_OK;
}

int ed25519_verify(const uint8_t *msg, size_t msg_len,
                   const uint8_t *signature, const uint8_t *public_key)
{
    if (!msg || !signature || !public_key) return SIGN_ERR_GENERIC;

#ifdef USE_LIBSODIUM
    if (crypto_sign_ed25519_verify_detached(signature, msg, msg_len, public_key) != 0) {
        return SIGN_ERR_SIGNATURE_MISMATCH;
    }
#else
    if (ed25519_verify_internal(signature, msg, msg_len, public_key) != 0) {
        return SIGN_ERR_SIGNATURE_MISMATCH;
    }
#endif

    return SIGN_OK;
}

int sign_save_keypair(const ed25519_keypair_t *kp,
                      const char *pub_path, const char *priv_path)
{
    if (!kp || !pub_path || !priv_path) return SIGN_ERR_GENERIC;

    FILE *fp = fopen(pub_path, "wb");
    if (!fp) {
        LOG_ERROR("Cannot create public key file: %s", pub_path);
        return SIGN_ERR_IO;
    }
    fwrite(kp->public_key, 1, ED25519_PUBLIC_KEY_SIZE, fp);
    fclose(fp);

    fp = fopen(priv_path, "wb");
    if (!fp) {
        LOG_ERROR("Cannot create secret key file: %s", priv_path);
        return SIGN_ERR_IO;
    }
    fwrite(kp->secret_key, 1, ED25519_SECRET_KEY_SIZE, fp);
    fclose(fp);

    LOG_INFO("Keypair saved: public=%s, private=%s", pub_path, priv_path);
    return SIGN_OK;
}

int sign_load_public_key(const char *path, uint8_t *public_key)
{
    if (!path || !public_key) return SIGN_ERR_GENERIC;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOG_ERROR("Cannot open public key file: %s", path);
        return SIGN_ERR_IO;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size != ED25519_PUBLIC_KEY_SIZE) {
        LOG_ERROR("Invalid public key size: %ld (expected %d)", size, ED25519_PUBLIC_KEY_SIZE);
        fclose(fp);
        return SIGN_ERR_INVALID_KEY;
    }

    size_t read = fread(public_key, 1, ED25519_PUBLIC_KEY_SIZE, fp);
    fclose(fp);

    if (read != ED25519_PUBLIC_KEY_SIZE) {
        LOG_ERROR("Failed to read public key");
        return SIGN_ERR_IO;
    }

    return SIGN_OK;
}

int sign_load_secret_key(const char *path, uint8_t *secret_key)
{
    if (!path || !secret_key) return SIGN_ERR_GENERIC;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOG_ERROR("Cannot open secret key file: %s", path);
        return SIGN_ERR_IO;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size != ED25519_SECRET_KEY_SIZE) {
        LOG_ERROR("Invalid secret key size: %ld (expected %d)", size, ED25519_SECRET_KEY_SIZE);
        fclose(fp);
        return SIGN_ERR_INVALID_KEY;
    }

    size_t read = fread(secret_key, 1, ED25519_SECRET_KEY_SIZE, fp);
    fclose(fp);

    if (read != ED25519_SECRET_KEY_SIZE) {
        LOG_ERROR("Failed to read secret key");
        return SIGN_ERR_IO;
    }

    return SIGN_OK;
}

int sign_save_signature(const uint8_t *signature, const char *path)
{
    if (!signature || !path) return SIGN_ERR_GENERIC;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        LOG_ERROR("Cannot create signature file: %s", path);
        return SIGN_ERR_IO;
    }

    fwrite(signature, 1, ED25519_SIGNATURE_SIZE, fp);
    fclose(fp);

    LOG_INFO("Signature saved: %s", path);
    return SIGN_OK;
}

int sign_load_signature(const char *path, uint8_t *signature)
{
    if (!path || !signature) return SIGN_ERR_GENERIC;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOG_ERROR("Cannot open signature file: %s", path);
        return SIGN_ERR_IO;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size != ED25519_SIGNATURE_SIZE) {
        LOG_ERROR("Invalid signature size: %ld (expected %d)", size, ED25519_SIGNATURE_SIZE);
        fclose(fp);
        return SIGN_ERR_GENERIC;
    }

    size_t read = fread(signature, 1, ED25519_SIGNATURE_SIZE, fp);
    fclose(fp);

    if (read != ED25519_SIGNATURE_SIZE) {
        LOG_ERROR("Failed to read signature");
        return SIGN_ERR_IO;
    }

    return SIGN_OK;
}

const char *sign_strerror(int err)
{
    switch (err) {
        case SIGN_OK:                    return "Success";
        case SIGN_ERR_GENERIC:           return "Generic error";
        case SIGN_ERR_INVALID_KEY:       return "Invalid key";
        case SIGN_ERR_SIGNATURE_MISMATCH: return "Signature mismatch";
        case SIGN_ERR_EXPIRED:           return "Certificate expired";
        case SIGN_ERR_UNAUTHORIZED:      return "Unauthorized vendor";
        case SIGN_ERR_IO:                return "I/O error";
        case SIGN_ERR_MEMORY:            return "Memory allocation error";
        default:                          return "Unknown error";
    }
}
