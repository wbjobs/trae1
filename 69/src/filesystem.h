#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "key_manager.h"
#include "user_manager.h"

namespace fusefs {

class FileSystem {
public:
    FileSystem(const std::string& root_dir, KeyManager& key_manager, UserManager& user_manager);
    ~FileSystem();

    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    std::string MapPath(const std::string& fuse_path) const;

    std::string EncryptPath(const std::string& fuse_path) const;
    std::string DecryptPath(const std::string& real_path) const;

    bool ReadFile(const std::string& fuse_path,
                  std::vector<uint8_t>& out_data,
                  size_t offset, size_t size) const;

    bool WriteFile(const std::string& fuse_path,
                   const std::vector<uint8_t>& data,
                   size_t offset, bool truncate);

    bool ReadFile(const std::string& fuse_path,
                  std::vector<uint8_t>& out_data,
                  const std::string& username) const;

    bool WriteFile(const std::string& fuse_path,
                   const std::vector<uint8_t>& data,
                   const std::string& username);

    bool FlushFile(const std::string& fuse_path);

    bool CreateFile(const std::string& fuse_path, mode_t mode);
    bool CreateSharedFile(const std::string& fuse_path, mode_t mode);
    bool CreateDirectory(const std::string& fuse_path, mode_t mode);
    bool RemoveFile(const std::string& fuse_path);
    bool RemoveDirectory(const std::string& fuse_path);
    bool Rename(const std::string& old_fuse_path,
                const std::string& new_fuse_path);
    bool TruncateFile(const std::string& fuse_path, off_t size);

    bool GetAttr(const std::string& fuse_path, struct stat& stbuf) const;
    bool ReadDir(const std::string& fuse_path,
                 std::vector<std::string>& entries) const;

    bool IsMetaPath(const std::string& fuse_path) const;

    bool IsFileShared(const std::string& fuse_path) const;

    bool ShareFile(const std::string& fuse_path, const std::string& username);

    bool UnshareFile(const std::string& fuse_path, const std::string& username);

    bool IsShared(const std::string& real_path) const;

    std::vector<std::string> GetSharedUsers(const std::string& real_path) const;

    std::vector<std::string> ScanSharedFiles(const std::string& username) const;

    std::vector<std::string> GetFileUsers(const std::string& fuse_path) const;

    bool ScanAndRepair(size_t& recovered, size_t& failed, size_t& total_tmp);

    const std::string& GetRootDir() const { return root_dir_; }

private:
    std::string EncryptComponent(const std::string& component,
                                 const std::string& parent_path) const;

    std::string DecryptComponent(const std::string& encrypted,
                                 const std::string& parent_path) const;

    std::vector<std::string> SplitPath(const std::string& path) const;
    std::string JoinPath(const std::vector<std::string>& components) const;

    std::string GetTempPath(const std::string& real_path) const;
    bool AtomicWrite(const std::string& real_path,
                     const std::vector<uint8_t>& ciphertext);

    void ScanDirAndRepair(const std::string& real_dir,
                          const std::string& fuse_parent,
                          size_t& recovered, size_t& failed, size_t& total_tmp);

    bool ReadFileKey(const std::string& real_path,
                     std::vector<uint8_t>& file_key) const;

    bool WriteFileKeys(const std::string& real_path,
                       const std::vector<uint8_t>& file_key,
                       const std::vector<std::string>& usernames);

    bool RemoveFileKey(const std::string& real_path, const std::string& username);

    bool GetXattr(const std::string& real_path, const std::string& name,
                  std::vector<uint8_t>& value) const;
    bool SetXattr(const std::string& real_path, const std::string& name,
                  const std::vector<uint8_t>& value);
    bool RemoveXattr(const std::string& real_path, const std::string& name);
    std::vector<std::string> ListXattrs(const std::string& real_path) const;

    bool SetSharedFlag(const std::string& real_path);
    bool ClearSharedFlag(const std::string& real_path);

    std::string root_dir_;
    KeyManager& key_manager_;
    UserManager& user_manager_;
};

}
