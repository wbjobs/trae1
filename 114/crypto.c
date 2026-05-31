#include "crypto.h"

int crypto_init(CryptoContext *ctx) {
    if (!RAND_bytes(ctx->key, AES_KEY_SIZE)) {
        fprintf(stderr, "Failed to generate AES key\n");
        return -1;
    }

    ctx->encrypt_ctx = EVP_CIPHER_CTX_new();
    ctx->decrypt_ctx = EVP_CIPHER_CTX_new();

    if (!ctx->encrypt_ctx || !ctx->decrypt_ctx) {
        fprintf(stderr, "Failed to create cipher contexts\n");
        crypto_cleanup(ctx);
        return -1;
    }

    return 0;
}

int crypto_encrypt(CryptoContext *ctx, const unsigned char *plaintext, int plaintext_len,
                   unsigned char *ciphertext, unsigned char *tag) {
    unsigned char iv[GCM_IV_SIZE];
    int len;
    int ciphertext_len;

    if (!RAND_bytes(iv, GCM_IV_SIZE)) {
        fprintf(stderr, "Failed to generate IV\n");
        return -1;
    }

    memcpy(ciphertext, iv, GCM_IV_SIZE);

    if (EVP_EncryptInit_ex(ctx->encrypt_ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1) {
        return -1;
    }

    if (EVP_EncryptInit_ex(ctx->encrypt_ctx, NULL, NULL, ctx->key, iv) != 1) {
        return -1;
    }

    if (EVP_EncryptUpdate(ctx->encrypt_ctx, ciphertext + GCM_IV_SIZE, &len, plaintext, plaintext_len) != 1) {
        return -1;
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx->encrypt_ctx, ciphertext + GCM_IV_SIZE + len, &len) != 1) {
        return -1;
    }
    ciphertext_len += len;

    if (EVP_CIPHER_CTX_ctrl(ctx->encrypt_ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_SIZE, tag) != 1) {
        return -1;
    }

    return ciphertext_len + GCM_IV_SIZE;
}

int crypto_decrypt(CryptoContext *ctx, const unsigned char *ciphertext, int ciphertext_len,
                   const unsigned char *tag, unsigned char *plaintext) {
    unsigned char iv[GCM_IV_SIZE];
    int len;
    int plaintext_len;

    memcpy(iv, ciphertext, GCM_IV_SIZE);

    if (EVP_DecryptInit_ex(ctx->decrypt_ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1) {
        return -1;
    }

    if (EVP_DecryptInit_ex(ctx->decrypt_ctx, NULL, NULL, ctx->key, iv) != 1) {
        return -1;
    }

    if (EVP_DecryptUpdate(ctx->decrypt_ctx, plaintext, &len, ciphertext + GCM_IV_SIZE, ciphertext_len - GCM_IV_SIZE) != 1) {
        return -1;
    }
    plaintext_len = len;

    if (EVP_CIPHER_CTX_ctrl(ctx->decrypt_ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_SIZE, (void *)tag) != 1) {
        return -1;
    }

    if (EVP_DecryptFinal_ex(ctx->decrypt_ctx, plaintext + len, &len) != 1) {
        fprintf(stderr, "Tag verification failed - data may be tampered\n");
        return -1;
    }
    plaintext_len += len;

    return plaintext_len;
}

void crypto_cleanup(CryptoContext *ctx) {
    if (ctx->encrypt_ctx) {
        EVP_CIPHER_CTX_free(ctx->encrypt_ctx);
        ctx->encrypt_ctx = NULL;
    }
    if (ctx->decrypt_ctx) {
        EVP_CIPHER_CTX_free(ctx->decrypt_ctx);
        ctx->decrypt_ctx = NULL;
    }
    memset(ctx->key, 0, AES_KEY_SIZE);
}
