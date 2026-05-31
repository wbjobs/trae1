#pragma once

#include <fuse3/fuse.h>

namespace fusefs {

struct FuseContext {
    void* filesystem;
    void* key_manager;
    void* user_manager;
};

void* FuseInit(struct fuse_conn_info* conn, struct fuse_config* cfg);
void FuseDestroy(void* private_data);

int FuseGetAttr(const char* path, struct stat* stbuf, struct fuse_file_info* fi);
int FuseReadDir(const char* path, void* buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info* fi,
                enum fuse_readdir_flags flags);
int FuseOpen(const char* path, struct fuse_file_info* fi);
int FuseRead(const char* path, char* buf, size_t size, off_t offset,
             struct fuse_file_info* fi);
int FuseWrite(const char* path, const char* buf, size_t size, off_t offset,
              struct fuse_file_info* fi);
int FuseCreate(const char* path, mode_t mode, struct fuse_file_info* fi);
int FuseMkDir(const char* path, mode_t mode);
int FuseUnlink(const char* path);
int FuseRmDir(const char* path);
int FuseRename(const char* from, const char* to, unsigned int flags);
int FuseTruncate(const char* path, off_t size, struct fuse_file_info* fi);
int FuseFlush(const char* path, struct fuse_file_info* fi);
int FuseRelease(const char* path, struct fuse_file_info* fi);
int FuseFsync(const char* path, int isdatasync, struct fuse_file_info* fi);
int FuseStatFs(const char* path, struct statvfs* stbuf);
int FuseAccess(const char* path, int mask);

}
