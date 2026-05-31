#ifndef CRYPTO_H
#define CRYPTO_H

#include "common.h"
#include <openssl/evp.h>
#include <openssl/rand.h>

#define AES_KEY_SIZE 16
#define GCM_IV_SIZE 12
#define GCM_TAG_SIZE 16

typedef struct {
    unsigned char key[AES_KEY_SIZE];
    EVP_CIPHER_CTX *encrypt_ctx;
    EVP_CIPHER_CTX *decrypt_ctx;
} CryptoContext;

int crypto_init(CryptoContext *ctx);
int crypto_encrypt(CryptoContext *ctx, const unsigned char *plaintext, int plaintext_len,
                   unsigned char *ciphertext, unsigned char *tag);
int crypto_decrypt(CryptoContext *ctx, const unsigned char *ciphertext, int ciphertext_len,
                   const unsigned char *tag, unsigned char *plaintext);
void crypto_cleanup(CryptoContext *ctx);

#endif
