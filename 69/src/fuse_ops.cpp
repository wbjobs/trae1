#include "fuse_ops.h"
#include "filesystem.h"
#include "key_manager.h"
#include "user_manager.h"
#include "config.h"

#include <cstring>
#include <errno.h>
#include <unistd.h>

namespace fusefs {

static FileSystem* GetFS(void* private_data) {
    auto* ctx = static_cast<FuseContext*>(private_data);
    return static_cast<FileSystem*>(ctx->filesystem);
}

static KeyManager* GetKM(void* private_data) {
    auto* ctx = static_cast<FuseContext*>(private_data);
    return static_cast<KeyManager*>(ctx->key_manager);
}

static UserManager* GetUM(void* private_data) {
    auto* ctx = static_cast<FuseContext*>(private_data);
    return static_cast<UserManager*>(ctx->user_manager);
}

void* FuseInit(struct fuse_conn_info* conn, struct fuse_config* cfg) {
    (void)conn;
    cfg->kernel_cache = 0;
    cfg->writeback_cache = 0;
    return fuse_get_context()->private_data;
}

void FuseDestroy(void* private_data) {
    auto* km = GetKM(private_data);
    if (km) {
        km->ClearKeys();
    }
    auto* um = GetUM(private_data);
    if (um) {
        um->ClearAll();
    }
}

int FuseGetAttr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    (void)fi;
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    std::memset(stbuf, 0, sizeof(struct stat));

    if (std::strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        stbuf->st_atime = time(nullptr);
        stbuf->st_mtime = time(nullptr);
        stbuf->st_ctime = time(nullptr);
        return 0;
    }

    if (fs->IsMetaPath(path)) return -ENOENT;

    struct stat real_stat;
    if (fs->GetAttr(path, real_stat)) {
        *stbuf = real_stat;
        return 0;
    }

    return -ENOENT;
}

int FuseReadDir(const char* path, void* buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info* fi,
                enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(path)) return -ENOENT;

    std::vector<std::string> entries;
    if (!fs->ReadDir(path, entries)) {
        return -ENOENT;
    }

    for (const auto& name : entries) {
        if (filler(buf, name.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0)) != 0) {
            return -ENOMEM;
        }
    }

    return 0;
}

int FuseOpen(const char* path, struct fuse_file_info* fi) {
    (void)fi;
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(path)) return -ENOENT;

    std::string real_path = fs->MapPath(path);

    int res = ::open(real_path.c_str(), fi->flags);
    if (res == -1) return -errno;

    ::close(res);
    fi->fh = 0;
    fi->keep_cache = 0;
    return 0;
}

int FuseRead(const char* path, char* buf, size_t size, off_t offset,
             struct fuse_file_info* fi) {
    (void)fi;
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(path)) return -ENOENT;

    std::vector<uint8_t> data;
    if (!fs->ReadFile(path, data, static_cast<size_t>(offset), size)) {
        return -EIO;
    }

    if (data.empty()) return 0;

    std::memcpy(buf, data.data(), data.size());
    return static_cast<int>(data.size());
}

int FuseWrite(const char* path, const char* buf, size_t size, off_t offset,
              struct fuse_file_info* fi) {
    (void)fi;
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(path)) return -ENOENT;

    std::vector<uint8_t> data(buf, buf + size);
    if (!fs->WriteFile(path, data, static_cast<size_t>(offset), false)) {
        return -EIO;
    }

    return static_cast<int>(size);
}

int FuseCreate(const char* path, mode_t mode, struct fuse_file_info* fi) {
    (void)fi;
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(path)) return -EACCES;

    if (!fs->CreateFile(path, mode)) {
        return -EIO;
    }

    return 0;
}

int FuseMkDir(const char* path, mode_t mode) {
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(path)) return -EACCES;

    if (!fs->CreateDirectory(path, mode)) {
        return -EIO;
    }

    return 0;
}

int FuseUnlink(const char* path) {
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(path)) return -EACCES;

    if (!fs->RemoveFile(path)) {
        return -errno;
    }

    return 0;
}

int FuseRmDir(const char* path) {
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(path)) return -EACCES;

    if (!fs->RemoveDirectory(path)) {
        return -errno;
    }

    return 0;
}

int FuseRename(const char* from, const char* to, unsigned int flags) {
    (void)flags;
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(from) || fs->IsMetaPath(to)) return -EACCES;

    if (!fs->Rename(from, to)) {
        return -errno;
    }

    return 0;
}

int FuseTruncate(const char* path, off_t size, struct fuse_file_info* fi) {
    (void)fi;
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(path)) return -EACCES;

    if (!fs->TruncateFile(path, size)) {
        return -EIO;
    }

    return 0;
}

int FuseFlush(const char* path, struct fuse_file_info* fi) {
    (void)fi;
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (std::strcmp(path, "/") == 0) return 0;
    if (fs->IsMetaPath(path)) return -EACCES;

    if (!fs->FlushFile(path)) {
        return -EIO;
    }

    return 0;
}

int FuseRelease(const char* path, struct fuse_file_info* fi) {
    (void)path;
    (void)fi;
    return 0;
}

int FuseFsync(const char* path, int isdatasync, struct fuse_file_info* fi) {
    (void)isdatasync;
    (void)fi;
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (std::strcmp(path, "/") == 0) return 0;
    if (fs->IsMetaPath(path)) return -EACCES;

    if (!fs->FlushFile(path)) {
        return -EIO;
    }

    return 0;
}

int FuseStatFs(const char* path, struct statvfs* stbuf) {
    (void)path;
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    std::string root = fs->GetRootDir();
    if (::statvfs(root.c_str(), stbuf) != 0) {
        return -errno;
    }

    return 0;
}

int FuseAccess(const char* path, int mask) {
    auto* fs = GetFS(fuse_get_context()->private_data);
    if (!fs) return -EIO;

    if (fs->IsMetaPath(path)) return -EACCES;

    if (std::strcmp(path, "/") == 0) return 0;

    std::string real_path = fs->MapPath(path);
    return ::access(real_path.c_str(), mask) == 0 ? 0 : -errno;
}

}
