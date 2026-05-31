#ifndef SIGN_H
#define SIGN_H

#include <stdint.h>
#include <stddef.h>

#define ED25519_PUBLIC_KEY_SIZE  32
#define ED25519_SECRET_KEY_SIZE  64
#define ED25519_SIGNATURE_SIZE   64
#define ED25519_SEED_SIZE        32

#define SIGN_OK                  0
#define SIGN_ERR_GENERIC        -1
#define SIGN_ERR_INVALID_KEY    -2
#define SIGN_ERR_SIGNATURE_MISMATCH -3
#define SIGN_ERR_EXPIRED        -4
#define SIGN_ERR_UNAUTHORIZED   -5
#define SIGN_ERR_IO             -6
#define SIGN_ERR_MEMORY         -7

typedef struct {
    uint8_t public_key[ED25519_PUBLIC_KEY_SIZE];
    uint8_t secret_key[ED25519_SECRET_KEY_SIZE];
} ed25519_keypair_t;

int  sign_init(void);
void sign_cleanup(void);

int  ed25519_generate_keypair(ed25519_keypair_t *kp);
int  ed25519_generate_keypair_from_seed(ed25519_keypair_t *kp, const uint8_t *seed);

int  ed25519_sign(const uint8_t *msg, size_t msg_len,
                  const uint8_t *secret_key, uint8_t *signature);
int  ed25519_verify(const uint8_t *msg, size_t msg_len,
                    const uint8_t *signature, const uint8_t *public_key);

int  sign_save_keypair(const ed25519_keypair_t *kp,
                       const char *pub_path, const char *priv_path);
int  sign_load_public_key(const char *path, uint8_t *public_key);
int  sign_load_secret_key(const char *path, uint8_t *secret_key);

int  sign_save_signature(const uint8_t *signature, const char *path);
int  sign_load_signature(const char *path, uint8_t *signature);

const char *sign_strerror(int err);

#endif
