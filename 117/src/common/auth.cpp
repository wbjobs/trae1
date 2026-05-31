#include "common/auth.h"
#include <sstream>
#include <algorithm>
#include <random>

namespace moshpp {

AuthManager::AuthManager() 
    : oauth2_configured_(false)
    , token_expiry_seconds_(3600) {
}

AuthManager::~AuthManager() = default;

void AuthManager::set_oauth2_config(const OAuth2Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    oauth2_config_ = config;
    oauth2_configured_ = true;
}

AuthToken AuthManager::create_token(const std::string& user_id, 
                                     const std::string& session_id,
                                     const std::string& device_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    AuthToken token;
    token.token_id = crypto_.generate_random_id(32);
    token.user_id = user_id;
    token.session_id = session_id;
    token.device_id = device_id.empty() ? crypto_.generate_random_id(16) : device_id;
    token.issued_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    token.expires_at = token.issued_at + token_expiry_seconds_;
    token.permissions = {"attach", "read", "write"};
    token.is_valid = true;
    
    active_tokens_[token.token_id] = token;
    
    if (user_tokens_.find(user_id) == user_tokens_.end()) {
        user_tokens_[user_id] = std::vector<std::string>();
    }
    user_tokens_[user_id].push_back(token.token_id);
    
    auto it = registered_users_.find(user_id);
    if (it != registered_users_.end()) {
        it->second.last_login = token.issued_at;
    }
    
    return token;
}

bool AuthManager::validate_token(const std::string& token_str) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    AuthToken token = deserialize_token(token_str);
    if (!token.is_valid) return false;
    
    auto it = active_tokens_.find(token.token_id);
    if (it == active_tokens_.end()) return false;
    if (!it->second.is_valid) return false;
    
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now > it->second.expires_at) {
        it->second.is_valid = false;
        return false;
    }
    
    return true;
}

AuthToken AuthManager::parse_token(const std::string& token_str) {
    std::lock_guard<std::mutex> lock(mutex_);
    return deserialize_token(token_str);
}

bool AuthManager::revoke_token(const std::string& token_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_tokens_.find(token_id);
    if (it == active_tokens_.end()) return false;
    
    it->second.is_valid = false;
    active_tokens_.erase(it);
    
    for (auto& [user_id, tokens] : user_tokens_) {
        tokens.erase(std::remove(tokens.begin(), tokens.end(), token_id), tokens.end());
    }
    
    return true;
}

bool AuthManager::revoke_user_tokens(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = user_tokens_.find(user_id);
    if (it == user_tokens_.end()) return false;
    
    for (const auto& token_id : it->second) {
        auto token_it = active_tokens_.find(token_id);
        if (token_it != active_tokens_.end()) {
            token_it->second.is_valid = false;
            active_tokens_.erase(token_it);
        }
    }
    
    user_tokens_.erase(it);
    return true;
}

UserInfo AuthManager::get_user_info(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = registered_users_.find(user_id);
    if (it != registered_users_.end()) {
        return it->second;
    }
    return UserInfo{};
}

bool AuthManager::register_user(const UserInfo& user_info) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (registered_users_.find(user_info.user_id) != registered_users_.end()) {
        return false;
    }
    
    registered_users_[user_info.user_id] = user_info;
    return true;
}

bool AuthManager::update_user_info(const UserInfo& user_info) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = registered_users_.find(user_info.user_id);
    if (it == registered_users_.end()) return false;
    
    it->second = user_info;
    return true;
}

std::string AuthManager::get_device_id(const std::string& device_name) {
    if (!device_name.empty()) {
        return crypto_.hash_string(device_name + std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 255);
    
    std::string id;
    for (int i = 0; i < 16; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02x", dist(gen));
        id += buf;
    }
    return id;
}

std::string AuthManager::generate_auth_url(const std::string& state) {
    if (!oauth2_configured_) return "";
    
    return oauth2_config_.auth_endpoint + 
           "?client_id=" + oauth2_config_.client_id +
           "&redirect_uri=" + oauth2_config_.redirect_uri +
           "&response_type=code" +
           "&scope=" + oauth2_config_.scope +
           "&state=" + state;
}

bool AuthManager::exchange_code_for_token(const std::string& code, std::string& token_str) {
    if (!oauth2_configured_) return false;
    
    AuthToken token;
    token.token_id = crypto_.generate_random_id(32);
    token.user_id = "oauth2_" + crypto_.hash_string(code);
    token.issued_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    token.expires_at = token.issued_at + token_expiry_seconds_;
    token.permissions = {"attach", "read", "write"};
    token.is_valid = true;
    
    std::lock_guard<std::mutex> lock(mutex_);
    active_tokens_[token.token_id] = token;
    token_str = sign_token(token);
    
    return true;
}

bool AuthManager::refresh_token(const std::string& refresh_token, std::string& new_token_str) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    AuthToken old_token = deserialize_token(refresh_token);
    if (!old_token.is_valid) return false;
    
    AuthToken new_token = old_token;
    new_token.token_id = crypto_.generate_random_id(32);
    new_token.issued_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    new_token.expires_at = new_token.issued_at + token_expiry_seconds_;
    
    active_tokens_[new_token.token_id] = new_token;
    new_token_str = sign_token(new_token);
    
    auto it = active_tokens_.find(old_token.token_id);
    if (it != active_tokens_.end()) {
        it->second.is_valid = false;
    }
    
    return true;
}

std::string AuthManager::sign_token(const AuthToken& token) {
    std::string serialized = serialize_token(token);
    std::string signature = crypto_.hash_string(serialized + "moshpp_secret");
    return serialized + "." + signature;
}

bool AuthManager::verify_token_signature(const std::string& token_str) {
    size_t dot_pos = token_str.find_last_of('.');
    if (dot_pos == std::string::npos) return false;
    
    std::string serialized = token_str.substr(0, dot_pos);
    std::string signature = token_str.substr(dot_pos + 1);
    std::string expected = crypto_.hash_string(serialized + "moshpp_secret");
    
    return signature == expected;
}

std::string AuthManager::serialize_token(const AuthToken& token) {
    std::ostringstream oss;
    oss << token.token_id << ":"
        << token.user_id << ":"
        << token.session_id << ":"
        << token.device_id << ":"
        << token.issued_at << ":"
        << token.expires_at << ":";
    
    for (size_t i = 0; i < token.permissions.size(); i++) {
        if (i > 0) oss << ",";
        oss << token.permissions[i];
    }
    
    return oss.str();
}

AuthToken AuthManager::deserialize_token(const std::string& token_str) {
    AuthToken token;
    token.is_valid = false;
    
    std::string data = token_str;
    size_t dot_pos = data.find_last_of('.');
    if (dot_pos != std::string::npos) {
        data = data.substr(0, dot_pos);
    }
    
    std::istringstream iss(data);
    std::string part;
    std::vector<std::string> parts;
    
    while (std::getline(iss, part, ':')) {
        parts.push_back(part);
    }
    
    if (parts.size() < 6) return token;
    
    token.token_id = parts[0];
    token.user_id = parts[1];
    token.session_id = parts[2];
    token.device_id = parts[3];
    token.issued_at = std::stoull(parts[4]);
    token.expires_at = std::stoull(parts[5]);
    
    if (parts.size() > 6) {
        std::istringstream perm_stream(parts[6]);
        std::string perm;
        while (std::getline(perm_stream, perm, ',')) {
            token.permissions.push_back(perm);
        }
    }
    
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    token.is_valid = (now <= token.expires_at);
    
    return token;
}

PermissionManager::PermissionManager() 
    : max_concurrent_devices_(10) {
}

PermissionManager::~PermissionManager() = default;

bool PermissionManager::create_session_permission(const std::string& session_id, 
                                                   const std::string& owner_user_id,
                                                   SessionAccessMode access_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (session_permissions_.find(session_id) != session_permissions_.end()) {
        return false;
    }
    
    SessionPermission perm;
    perm.session_id = session_id;
    perm.owner_user_id = owner_user_id;
    perm.access_mode = access_mode;
    perm.allowed_users = {owner_user_id};
    perm.user_permissions[owner_user_id] = {"attach", "read", "write", "steal", "share"};
    perm.created_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    perm.last_modified = perm.created_at;
    perm.max_concurrent_devices = max_concurrent_devices_;
    perm.allow_steal = true;
    
    session_permissions_[session_id] = perm;
    return true;
}

bool PermissionManager::check_permission(const std::string& session_id, 
                                          const std::string& user_id,
                                          const std::string& permission) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = session_permissions_.find(session_id);
    if (it == session_permissions_.end()) return false;
    
    const auto& perm = it->second;
    
    if (perm.owner_user_id == user_id) return true;
    
    auto user_it = perm.user_permissions.find(user_id);
    if (user_it == perm.user_permissions.end()) return false;
    
    const auto& permissions = user_it->second;
    return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
}

bool PermissionManager::grant_permission(const std::string& session_id,
                                          const std::string& user_id,
                                          const std::vector<std::string>& permissions) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = session_permissions_.find(session_id);
    if (it == session_permissions_.end()) return false;
    
    it->second.user_permissions[user_id] = permissions;
    
    auto allowed_it = std::find(it->second.allowed_users.begin(), 
                                it->second.allowed_users.end(), user_id);
    if (allowed_it == it->second.allowed_users.end()) {
        it->second.allowed_users.push_back(user_id);
    }
    
    it->second.last_modified = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return true;
}

bool PermissionManager::revoke_permission(const std::string& session_id,
                                           const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = session_permissions_.find(session_id);
    if (it == session_permissions_.end()) return false;
    
    if (it->second.owner_user_id == user_id) return false;
    
    it->second.user_permissions.erase(user_id);
    it->second.allowed_users.erase(
        std::remove(it->second.allowed_users.begin(), 
                   it->second.allowed_users.end(), user_id),
        it->second.allowed_users.end());
    
    it->second.last_modified = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return true;
}

bool PermissionManager::set_access_mode(const std::string& session_id, SessionAccessMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = session_permissions_.find(session_id);
    if (it == session_permissions_.end()) return false;
    
    it->second.access_mode = mode;
    it->second.allow_steal = (mode == SessionAccessMode::STEAL || 
                             mode == SessionAccessMode::SHARED);
    it->second.last_modified = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return true;
}

SessionAccessMode PermissionManager::get_access_mode(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = session_permissions_.find(session_id);
    if (it == session_permissions_.end()) return SessionAccessMode::EXCLUSIVE;
    
    return it->second.access_mode;
}

bool PermissionManager::is_owner(const std::string& session_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = session_permissions_.find(session_id);
    if (it == session_permissions_.end()) return false;
    
    return it->second.owner_user_id == user_id;
}

std::string PermissionManager::get_owner(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = session_permissions_.find(session_id);
    if (it == session_permissions_.end()) return "";
    
    return it->second.owner_user_id;
}

bool PermissionManager::can_steal(const std::string& session_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = session_permissions_.find(session_id);
    if (it == session_permissions_.end()) return false;
    
    const auto& perm = it->second;
    
    if (perm.owner_user_id == user_id) return true;
    if (!perm.allow_steal) return false;
    
    auto user_it = perm.user_permissions.find(user_id);
    if (user_it == perm.user_permissions.end()) return false;
    
    const auto& permissions = user_it->second;
    return std::find(permissions.begin(), permissions.end(), "steal") != permissions.end();
}

bool PermissionManager::can_share(const std::string& session_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = session_permissions_.find(session_id);
    if (it == session_permissions_.end()) return false;
    
    const auto& perm = it->second;
    
    if (perm.owner_user_id == user_id) return true;
    if (perm.access_mode != SessionAccessMode::SHARED) return false;
    
    auto user_it = perm.user_permissions.find(user_id);
    if (user_it == perm.user_permissions.end()) return false;
    
    const auto& permissions = user_it->second;
    return std::find(permissions.begin(), permissions.end(), "share") != permissions.end();
}

bool PermissionManager::delete_session_permission(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = session_permissions_.find(session_id);
    if (it == session_permissions_.end()) return false;
    
    session_permissions_.erase(it);
    return true;
}

}
