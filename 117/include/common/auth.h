#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <mutex>
#include <memory>
#include <chrono>
#include <functional>
#include "crypto.h"

namespace moshpp {

struct UserInfo {
    std::string user_id;
    std::string username;
    std::string email;
    std::string display_name;
    std::vector<std::string> roles;
    uint64_t created_at;
    uint64_t last_login;
    bool is_active;
};

struct AuthToken {
    std::string token_id;
    std::string user_id;
    std::string session_id;
    std::string device_id;
    std::string device_name;
    uint64_t issued_at;
    uint64_t expires_at;
    std::vector<std::string> permissions;
    bool is_valid;
};

struct OAuth2Config {
    std::string client_id;
    std::string client_secret;
    std::string auth_endpoint;
    std::string token_endpoint;
    std::string userinfo_endpoint;
    std::string redirect_uri;
    std::string scope;
};

class AuthManager {
public:
    AuthManager();
    ~AuthManager();

    void set_oauth2_config(const OAuth2Config& config);
    bool has_oauth2_config() const { return oauth2_configured_; }

    AuthToken create_token(const std::string& user_id, 
                           const std::string& session_id = "",
                           const std::string& device_id = "");
    
    bool validate_token(const std::string& token_str);
    AuthToken parse_token(const std::string& token_str);
    
    bool revoke_token(const std::string& token_id);
    bool revoke_user_tokens(const std::string& user_id);

    UserInfo get_user_info(const std::string& user_id);
    bool register_user(const UserInfo& user_info);
    bool update_user_info(const UserInfo& user_info);

    std::string get_device_id(const std::string& device_name = "");
    std::string generate_auth_url(const std::string& state);

    bool exchange_code_for_token(const std::string& code, std::string& token_str);
    bool refresh_token(const std::string& refresh_token, std::string& new_token_str);

    void set_token_expiry(uint64_t seconds) { token_expiry_seconds_ = seconds; }
    uint64_t get_token_expiry() const { return token_expiry_seconds_; }

private:
    OAuth2Config oauth2_config_;
    bool oauth2_configured_;
    uint64_t token_expiry_seconds_;
    
    std::map<std::string, AuthToken> active_tokens_;
    std::map<std::string, UserInfo> registered_users_;
    std::map<std::string, std::vector<std::string>> user_tokens_;
    std::mutex mutex_;
    Crypto crypto_;

    std::string sign_token(const AuthToken& token);
    bool verify_token_signature(const std::string& token_str);
    std::string serialize_token(const AuthToken& token);
    AuthToken deserialize_token(const std::string& token_str);
};

enum class SessionAccessMode {
    EXCLUSIVE = 0,
    STEAL = 1,
    SHARED = 2
};

struct SessionPermission {
    std::string session_id;
    std::string owner_user_id;
    SessionAccessMode access_mode;
    std::vector<std::string> allowed_users;
    std::map<std::string, std::vector<std::string>> user_permissions;
    uint64_t created_at;
    uint64_t last_modified;
    int max_concurrent_devices;
    bool allow_steal;
};

class PermissionManager {
public:
    PermissionManager();
    ~PermissionManager();

    bool create_session_permission(const std::string& session_id, 
                                   const std::string& owner_user_id,
                                   SessionAccessMode access_mode = SessionAccessMode::STEAL);
    
    bool check_permission(const std::string& session_id, 
                          const std::string& user_id,
                          const std::string& permission = "attach");
    
    bool grant_permission(const std::string& session_id,
                          const std::string& user_id,
                          const std::vector<std::string>& permissions);
    
    bool revoke_permission(const std::string& session_id,
                           const std::string& user_id);
    
    bool set_access_mode(const std::string& session_id, SessionAccessMode mode);
    SessionAccessMode get_access_mode(const std::string& session_id);
    
    bool is_owner(const std::string& session_id, const std::string& user_id);
    std::string get_owner(const std::string& session_id);
    
    bool can_steal(const std::string& session_id, const std::string& user_id);
    bool can_share(const std::string& session_id, const std::string& user_id);
    
    bool delete_session_permission(const std::string& session_id);
    
    void set_max_concurrent_devices(int max) { max_concurrent_devices_ = max; }

private:
    std::map<std::string, SessionPermission> session_permissions_;
    int max_concurrent_devices_;
    mutable std::mutex mutex_;
};

}
