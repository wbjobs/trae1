#ifndef AUTH_H
#define AUTH_H

#include "common.h"

typedef enum {
    AUTH_TYPE_NTLMv2,
    AUTH_TYPE_KERBEROS
} AuthType;

typedef struct {
    AuthType type;
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char domain[MAX_USERNAME];
} AuthContext;

int auth_init(AuthContext *ctx, const char *username, const char *password, AuthType type);
int auth_verify_credentials(AuthContext *ctx, const char *provided_username, const char *provided_password);
void auth_cleanup(AuthContext *ctx);
int auth_generate_ntlmv2_challenge(unsigned char *challenge, int challenge_len);
int auth_verify_ntlmv2_response(AuthContext *ctx, const unsigned char *response, int response_len,
                                const unsigned char *challenge);

#endif
