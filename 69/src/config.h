#pragma once

#include <cstddef>
#include <cstdint>

namespace fusefs {

constexpr size_t PBKDF2_ITERATIONS = 100000;
constexpr size_t SALT_LEN = 16;
constexpr size_t KEY_LEN = 32;
constexpr size_t GCM_IV_LEN = 12;
constexpr size_t GCM_TAG_LEN = 16;
constexpr size_t FILE_HEADER_LEN = GCM_IV_LEN + GCM_TAG_LEN;

constexpr const char* META_DIR = ".fuse_enc_meta";
constexpr const char* SALT_FILE = "salt";
constexpr const char* CONFIG_FILE = "config";
constexpr const char* USERS_DIR = "users";
constexpr const char* USER_REGISTRY = "registry";

constexpr size_t FPE_ROUNDS = 10;
constexpr size_t FPE_BLOCK_SIZE = 16;

constexpr size_t MAX_FILENAME_LEN = 255;
constexpr size_t MAX_PATH_LEN = 4096;

constexpr size_t RSA_KEY_BITS = 2048;
constexpr const char* OWNER_USER = "owner";

constexpr const char* XATTR_SHARED_FLAG = "user.fuse_enc.shared";
constexpr const char* XATTR_KEY_PREFIX = "user.fuse_enc.key.";
constexpr const char* XATTR_OWNER = "user.fuse_enc.owner";

constexpr size_t MAX_XATTR_SIZE = 4096;

}
