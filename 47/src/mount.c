#include "mycontainer.h"
#include <sys/syscall.h>
#include <limits.h>

static int pivot_root(const char *new_root, const char *put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}

static int bind_mount(const char *src, const char *dst, unsigned long flags)
{
    if (mount(src, dst, NULL, MS_BIND | flags, NULL) < 0) {
        fprintf(stderr, "[mycontainer] bind mount %s -> %s failed: %s\n",
                src, dst, strerror(errno));
        return -1;
    }
    return 0;
}

int setup_mounts(const char *image_dir)
{
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
        fprintf(stderr, "[mycontainer] mount private / failed: %s\n",
                strerror(errno));
        return -1;
    }

    char resolved[PATH_MAX];
    if (!realpath(image_dir, resolved)) {
        fprintf(stderr, "[mycontainer] realpath %s: %s\n",
                image_dir, strerror(errno));
        return -1;
    }

    if (mount(resolved, resolved, NULL, MS_BIND | MS_REC, NULL) < 0) {
        fprintf(stderr, "[mycontainer] bind rootfs failed: %s\n",
                strerror(errno));
        return -1;
    }

    char put_old[PATH_MAX];
    snprintf(put_old, sizeof(put_old), "%s/.oldroot", resolved);
    if (mkdir(put_old, 0700) < 0 && errno != EEXIST) {
        fprintf(stderr, "[mycontainer] mkdir %s: %s\n", put_old, strerror(errno));
        return -1;
    }

    if (chdir(resolved) < 0) {
        perror("chdir");
        return -1;
    }
    if (pivot_root(".", put_old) < 0) {
        perror("pivot_root");
        return -1;
    }
    if (chdir("/") < 0) {
        perror("chdir /");
        return -1;
    }

    if (umount2("/.oldroot", MNT_DETACH) < 0) {
        fprintf(stderr, "[mycontainer] umount oldroot: %s\n", strerror(errno));
    }
    rmdir("/.oldroot");

    if (mkdir("/proc", 0555) < 0 && errno != EEXIST) {
        fprintf(stderr, "[mycontainer] mkdir /proc: %s\n", strerror(errno));
    }
    if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
        fprintf(stderr, "[mycontainer] mount proc: %s\n", strerror(errno));
    }

    if (mkdir("/sys", 0555) < 0 && errno != EEXIST) {
        fprintf(stderr, "[mycontainer] mkdir /sys: %s\n", strerror(errno));
    }
    if (mount("sysfs", "/sys", "sysfs", MS_RDONLY, NULL) < 0) {
        fprintf(stderr, "[mycontainer] mount sysfs: %s (continuing)\n",
                strerror(errno));
    }

    if (mkdir("/dev", 0555) < 0 && errno != EEXIST) {
        fprintf(stderr, "[mycontainer] mkdir /dev: %s\n", strerror(errno));
    }
    if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME,
              "mode=755,size=65536k") < 0) {
        fprintf(stderr, "[mycontainer] mount tmpfs /dev: %s (continuing)\n",
                strerror(errno));
    }

    (void)bind_mount;
    return 0;
}

int setup_hostname(void)
{
    if (sethostname("mycontainer", 11) < 0) {
        perror("sethostname");
        return -1;
    }
    return 0;
}
