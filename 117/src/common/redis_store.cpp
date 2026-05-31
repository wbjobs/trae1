#include "common/redis_store.h"
#include <chrono>
#include <algorithm>
#include <sstream>

namespace moshpp {

RedisSessionStore::RedisSessionStore()
    : redis_host_("localhost")
    , redis_port_(6379)
    , redis_db_(0)
    , connected_(false)
    , use_local_(true)
    , ttl_seconds_(86400)
    , redis_context_(nullptr) {
}

RedisSessionStore::~RedisSessionStore() {
    disconnect();
}

bool RedisSessionStore::connect(const std::string& host, int port, 
                                const std::string& password, int db) {
    redis_host_ = host;
    redis_port_ = port;
    redis_password_ = password;
    redis_db_ = db;
    
    if (use_local_) {
        connected_ = true;
        return true;
    }
    
    connected_ = false;
    return false;
}

void RedisSessionStore::disconnect() {
    if (redis_context_) {
        redis_context_ = nullptr;
    }
    connected_ = false;
}

bool RedisSessionStore::save_session_state(const std::string& session_id, 
                                             const SessionDeviceState& state) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        local_session_states_[session_id] = state;
        return true;
    }
    return false;
}

SessionDeviceState RedisSessionStore::load_session_state(const std::string& session_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_session_states_.find(session_id);
        if (it != local_session_states_.end()) {
            return it->second;
        }
    }
    return SessionDeviceState{};
}

bool RedisSessionStore::delete_session_state(const std::string& session_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        local_session_states_.erase(session_id);
        local_shell_states_.erase(session_id);
        local_output_.erase(session_id);
        return true;
    }
    return false;
}

bool RedisSessionStore::session_exists(const std::string& session_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        return local_session_states_.find(session_id) != local_session_states_.end();
    }
    return false;
}

bool RedisSessionStore::add_device(const std::string& session_id, 
                                      const DeviceInfo& device) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_session_states_.find(session_id);
        if (it == local_session_states_.end()) {
            return false;
        }
        
        auto& devices = it->second.connected_devices;
        auto existing = std::find_if(devices.begin(), devices.end(),
            [&device](const DeviceInfo& d) { return d.device_id == device.device_id; });
        
        if (existing != devices.end()) {
            *existing = device;
        } else {
            devices.push_back(device);
        }
        
        it->second.last_updated = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        it->second.active_device_count++;
        
        return true;
    }
    return false;
}

bool RedisSessionStore::remove_device(const std::string& session_id, 
                                         const std::string& device_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_session_states_.find(session_id);
        if (it == local_session_states_.end()) {
            return false;
        }
        
        auto& devices = it->second.connected_devices;
        auto to_remove = std::find_if(devices.begin(), devices.end(),
            [&device_id](const DeviceInfo& d) { return d.device_id == device_id; });
        
        if (to_remove != devices.end()) {
            devices.erase(to_remove);
            it->second.active_device_count--;
            
            if (it->second.primary_device_id == device_id) {
                if (!devices.empty()) {
                    it->second.primary_device_id = devices.front().device_id;
                } else {
                    it->second.primary_device_id = "";
                }
            }
            
            it->second.last_updated = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            return true;
        }
    }
    return false;
}

bool RedisSessionStore::update_device(const std::string& session_id,
                                         const DeviceInfo& device) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_session_states_.find(session_id);
        if (it == local_session_states_.end()) {
            return false;
        }
        
        auto& devices = it->second.connected_devices;
        auto existing = std::find_if(devices.begin(), devices.end(),
            [&device](const DeviceInfo& d) { return d.device_id == device.device_id; });
        
        if (existing != devices.end()) {
            *existing = device;
            it->second.last_updated = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            return true;
        }
    }
    return false;
}

std::vector<DeviceInfo> RedisSessionStore::get_devices(const std::string& session_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_session_states_.find(session_id);
        if (it != local_session_states_.end()) {
            return it->second.connected_devices;
        }
    }
    return {};
}

DeviceInfo RedisSessionStore::get_primary_device(const std::string& session_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_session_states_.find(session_id);
        if (it != local_session_states_.end()) {
            const auto& devices = it->second.connected_devices;
            auto primary = std::find_if(devices.begin(), devices.end(),
                [&it](const DeviceInfo& d) { 
                    return d.device_id == it->second.primary_device_id; 
                });
            
            if (primary != devices.end()) {
                return *primary;
            }
            
            if (!devices.empty()) {
                return devices.front();
            }
        }
    }
    return DeviceInfo{};
}

bool RedisSessionStore::set_primary_device(const std::string& session_id, 
                                             const std::string& device_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_session_states_.find(session_id);
        if (it == local_session_states_.end()) {
            return false;
        }
        
        it->second.primary_device_id = device_id;
        
        for (auto& device : it->second.connected_devices) {
            if (device.device_id == device_id) {
                device.is_readonly = false;
            } else if (it->second.access_mode != SessionAccessMode::SHARED) {
                device.is_readonly = true;
            }
        }
        
        it->second.last_updated = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return true;
    }
    return false;
}

bool RedisSessionStore::save_shell_state(const std::string& session_id, 
                                           const ShellState& state) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        local_shell_states_[session_id] = state;
        return true;
    }
    return false;
}

ShellState RedisSessionStore::load_shell_state(const std::string& session_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_shell_states_.find(session_id);
        if (it != local_shell_states_.end()) {
            return it->second;
        }
    }
    return ShellState{};
}

bool RedisSessionStore::save_output_chunk(const std::string& session_id, 
                                             uint64_t offset, 
                                             const std::string& data) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        local_output_[session_id][offset] = data;
        return true;
    }
    return false;
}

std::string RedisSessionStore::load_output_chunk(const std::string& session_id, 
                                                    uint64_t offset, 
                                                    size_t length) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto session_it = local_output_.find(session_id);
        if (session_it == local_output_.end()) {
            return "";
        }
        
        std::string result;
        auto it = session_it->second.lower_bound(offset);
        size_t remaining = length;
        
        while (remaining > 0 && it != session_it->second.end()) {
            if (it->second.size() <= remaining) {
                result += it->second;
                remaining -= it->second.size();
                it++;
            } else {
                result += it->second.substr(0, remaining);
                remaining = 0;
            }
        }
        
        return result;
    }
    return "";
}

uint64_t RedisSessionStore::get_output_size(const std::string& session_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_output_.find(session_id);
        if (it == local_output_.end() || it->second.empty()) {
            return 0;
        }
        
        auto last = it->second.rbegin();
        return last->first + last->second.size();
    }
    return 0;
}

bool RedisSessionStore::publish_to_channel(const std::string& channel, 
                                              const std::string& message) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_subscribers_.find(channel);
        if (it != local_subscribers_.end() && it->second) {
            it->second(message);
            return true;
        }
    }
    return false;
}

bool RedisSessionStore::subscribe_channel(const std::string& channel, 
                                            std::function<void(const std::string&)> callback) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        local_subscribers_[channel] = callback;
        return true;
    }
    return false;
}

bool RedisSessionStore::unsubscribe_channel(const std::string& channel) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        local_subscribers_.erase(channel);
        return true;
    }
    return false;
}

bool RedisSessionStore::save_user_sessions(const std::string& user_id, 
                                              const std::vector<std::string>& session_ids) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        local_user_sessions_[user_id] = session_ids;
        return true;
    }
    return false;
}

std::vector<std::string> RedisSessionStore::get_user_sessions(const std::string& user_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_user_sessions_.find(user_id);
        if (it != local_user_sessions_.end()) {
            return it->second;
        }
    }
    return {};
}

bool RedisSessionStore::remove_user_session(const std::string& user_id, 
                                              const std::string& session_id) {
    if (use_local_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = local_user_sessions_.find(user_id);
        if (it == local_user_sessions_.end()) {
            return false;
        }
        
        auto& sessions = it->second;
        sessions.erase(std::remove(sessions.begin(), sessions.end(), session_id), sessions.end());
        return true;
    }
    return false;
}

RoamingManager::RoamingManager()
    : max_devices_(10) {
}

RoamingManager::~RoamingManager() = default;

bool RoamingManager::handle_device_join(const std::string& session_id,
                                         const DeviceInfo& device,
                                         const std::string& user_id,
                                         SessionAccessMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!store_) return false;
    
    if (!store_->session_exists(session_id)) {
        SessionDeviceState state;
        state.session_id = session_id;
        state.owner_user_id = user_id;
        state.primary_device_id = device.device_id;
        state.created_at = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        state.last_updated = state.created_at;
        state.is_active = true;
        
        store_->save_session_state(session_id, state);
    }
    
    if (permission_mgr_) {
        if (!permission_mgr_->check_permission(session_id, user_id, "attach")) {
            return false;
        }
    }
    
    auto devices = store_->get_devices(session_id);
    bool is_new = std::none_of(devices.begin(), devices.end(),
        [&device](const DeviceInfo& d) { return d.device_id == device.device_id; });
    
    DeviceInfo updated_device = device;
    updated_device.connected_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    updated_device.last_active = updated_device.connected_at;
    updated_device.is_active = true;
    
    if (mode == SessionAccessMode::STEAL) {
        updated_device.is_readonly = false;
        
        for (auto& d : devices) {
            if (d.device_id != device.device_id) {
                d.is_readonly = true;
                store_->update_device(session_id, d);
                
                RoamingPacket notification;
                notification.type = RoamingPacket::NOTIFICATION;
                notification.session_id = session_id;
                notification.device_id = d.device_id;
                notification.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                notification.payload = "Session stolen by " + device.device_name;
                
                std::ostringstream oss;
                oss << static_cast<int>(notification.type) << ":" 
                    << notification.session_id << ":" 
                    << notification.device_id << ":" 
                    << notification.payload;
                
                store_->publish_to_channel("session:" + session_id, oss.str());
            }
        }
        
        store_->set_primary_device(session_id, device.device_id);
    } else if (mode == SessionAccessMode::SHARED) {
        updated_device.is_readonly = false;
    } else {
        updated_device.is_readonly = true;
    }
    
    if (is_new) {
        store_->add_device(session_id, updated_device);
    } else {
        store_->update_device(session_id, updated_device);
    }
    
    return true;
}

bool RoamingManager::handle_device_leave(const std::string& session_id,
                                          const std::string& device_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!store_) return false;
    
    store_->remove_device(session_id, device_id);
    
    auto devices = store_->get_devices(session_id);
    if (devices.empty()) {
        store_->delete_session_state(session_id);
    }
    
    return true;
}

bool RoamingManager::handle_device_steal(const std::string& session_id,
                                          const DeviceInfo& new_device,
                                          const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!store_) return false;
    
    if (permission_mgr_) {
        if (!permission_mgr_->can_steal(session_id, user_id)) {
            return false;
        }
    }
    
    store_->set_primary_device(session_id, new_device.device_id);
    
    auto devices = store_->get_devices(session_id);
    for (auto& d : devices) {
        if (d.device_id != new_device.device_id) {
            d.is_readonly = true;
            store_->update_device(session_id, d);
            
            RoamingPacket notification;
            notification.type = RoamingPacket::NOTIFICATION;
            notification.session_id = session_id;
            notification.device_id = d.device_id;
            notification.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            notification.payload = "Session stolen by " + new_device.device_name;
            
            std::ostringstream oss;
            oss << static_cast<int>(notification.type) << ":" 
                << notification.session_id << ":" 
                << notification.device_id << ":" 
                << notification.payload;
            
            store_->publish_to_channel("session:" + session_id, oss.str());
        }
    }
    
    return true;
}

bool RoamingManager::handle_output_append(const std::string& session_id,
                                            uint64_t offset,
                                            const std::string& data) {
    if (!store_) return false;
    
    store_->save_output_chunk(session_id, offset, data);
    
    RoamingPacket packet;
    packet.type = RoamingPacket::OUTPUT_APPEND;
    packet.session_id = session_id;
    packet.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::ostringstream oss;
    oss << static_cast<int>(packet.type) << ":" 
        << packet.session_id << ":" 
        << offset << ":" 
        << data;
    
    store_->publish_to_channel("session:" + session_id + ":output", oss.str());
    
    return true;
}

bool RoamingManager::handle_state_update(const std::string& session_id,
                                          const ShellState& state) {
    if (!store_) return false;
    
    store_->save_shell_state(session_id, state);
    
    RoamingPacket packet;
    packet.type = RoamingPacket::STATE_UPDATE;
    packet.session_id = session_id;
    packet.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::ostringstream oss;
    oss << static_cast<int>(packet.type) << ":" 
        << packet.session_id << ":" 
        << state.current_directory;
    
    store_->publish_to_channel("session:" + session_id + ":state", oss.str());
    
    return true;
}

ShellState RoamingManager::get_current_state(const std::string& session_id) {
    if (!store_) return ShellState{};
    return store_->load_shell_state(session_id);
}

std::string RoamingManager::get_output_since(const std::string& session_id, uint64_t offset) {
    if (!store_) return "";
    
    uint64_t total_size = store_->get_output_size(session_id);
    if (offset >= total_size) return "";
    
    return store_->load_output_chunk(session_id, offset, total_size - offset);
}

bool RoamingManager::notify_device_readonly(const std::string& session_id,
                                              const std::string& device_id) {
    if (!store_) return false;
    
    auto devices = store_->get_devices(session_id);
    for (auto& d : devices) {
        if (d.device_id == device_id) {
            d.is_readonly = true;
            store_->update_device(session_id, d);
            return true;
        }
    }
    return false;
}

int RoamingManager::get_active_device_count(const std::string& session_id) {
    if (!store_) return 0;
    
    auto devices = store_->get_devices(session_id);
    return static_cast<int>(std::count_if(devices.begin(), devices.end(),
        [](const DeviceInfo& d) { return d.is_active; }));
}

bool RoamingManager::can_accept_new_device(const std::string& session_id,
                                            const std::string& user_id,
                                            SessionAccessMode mode) {
    if (permission_mgr_) {
        if (!permission_mgr_->check_permission(session_id, user_id, "attach")) {
            return false;
        }
        
        SessionAccessMode session_mode = permission_mgr_->get_access_mode(session_id);
        
        if (mode == SessionAccessMode::EXCLUSIVE) {
            if (get_active_device_count(session_id) > 0) {
                return false;
            }
        } else if (mode == SessionAccessMode::STEAL) {
            if (!permission_mgr_->can_steal(session_id, user_id)) {
                return false;
            }
        } else if (mode == SessionAccessMode::SHARED) {
            if (!permission_mgr_->can_share(session_id, user_id)) {
                return false;
            }
        }
    }
    
    return get_active_device_count(session_id) < max_devices_;
}

}
