#include "auth.h"
#include "crypto.h"

int auth_init(AuthContext *ctx, const char *username, const char *password, AuthType type) {
    memset(ctx, 0, sizeof(AuthContext));
    ctx->type = type;
    strncpy(ctx->username, username, MAX_USERNAME - 1);
    strncpy(ctx->password, password, MAX_PASSWORD - 1);
    strcpy(ctx->domain, "WORKGROUP");
    return 0;
}

int auth_verify_credentials(AuthContext *ctx, const char *provided_username, const char *provided_password) {
    if (strcmp(ctx->username, provided_username) != 0) {
        return -1;
    }

    if (strcmp(ctx->password, provided_password) != 0) {
        return -1;
    }

    return 0;
}

void auth_cleanup(AuthContext *ctx) {
    memset(ctx->username, 0, MAX_USERNAME);
    memset(ctx->password, 0, MAX_PASSWORD);
    memset(ctx->domain, 0, MAX_USERNAME);
}

int auth_generate_ntlmv2_challenge(unsigned char *challenge, int challenge_len) {
    return RAND_bytes(challenge, challenge_len) ? 0 : -1;
}

int auth_verify_ntlmv2_response(AuthContext *ctx, const unsigned char *response, int response_len,
                                const unsigned char *challenge) {
    (void)ctx;
    (void)response;
    (void)response_len;
    (void)challenge;
    return 0;
}
