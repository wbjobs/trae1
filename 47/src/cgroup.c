#include "mycontainer.h"
#include <libcgroup.h>

static int write_file(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0)
        return -1;
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    return (n < 0) ? -1 : 0;
}

static int cgroup_v2_controller_available(const char *controller)
{
    char buf[4096];
    int fd = open("/sys/fs/cgroup/cgroup.controllers", O_RDONLY);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return 0;
    buf[n] = '\0';
    return strstr(buf, controller) != NULL;
}

static char cgroup_path_buf[512];

static const char *cgroup_path(const char *name)
{
    snprintf(cgroup_path_buf, sizeof(cgroup_path_buf),
             "/sys/fs/cgroup/%s", name);
    return cgroup_path_buf;
}

int setup_cgroup(pid_t pid, const char *name)
{
    const char *path = cgroup_path(name);

    if (!cgroup_v2_controller_available("cpu") ||
        !cgroup_v2_controller_available("memory")) {
        fprintf(stderr,
                "[mycontainer] warning: cgroup v2 controllers cpu/memory "
                "not available, proceeding without resource limits\n");
    }

    if (mkdir(path, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "[mycontainer] warning: cannot create cgroup %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    char sub[512];
    snprintf(sub, sizeof(sub), "%s/cgroup.subtree_control", path);
    write_file(sub, "+cpu +memory");

    snprintf(sub, sizeof(sub), "%s/cgroup.procs", path);
    char pidbuf[32];
    snprintf(pidbuf, sizeof(pidbuf), "%d\n", pid);
    if (write_file(sub, pidbuf) < 0) {
        fprintf(stderr, "[mycontainer] warning: cannot add pid %d to %s: %s\n",
                pid, sub, strerror(errno));
    }

    snprintf(sub, sizeof(sub), "%s/cpu.max", path);
    char maxbuf[64];
    snprintf(maxbuf, sizeof(maxbuf), "%d %d\n",
             DEFAULT_CPU_QUOTA, DEFAULT_CPU_PERIOD);
    write_file(sub, maxbuf);

    snprintf(sub, sizeof(sub), "%s/memory.max", path);
    char membuf[64];
    snprintf(membuf, sizeof(membuf), "%lu\n", DEFAULT_MEM_LIMIT);
    write_file(sub, membuf);

    return 0;
}

void cleanup_cgroup(const char *name)
{
    const char *path = cgroup_path(name);

    char sub[512];
    snprintf(sub, sizeof(sub), "%s/cgroup.kill", path);
    int fd = open(sub, O_WRONLY);
    if (fd >= 0) {
        write(fd, "1\n", 2);
        close(fd);
    }

    for (int i = 0; i < 50; ++i) {
        if (rmdir(path) == 0)
            return;
        if (errno == ENOENT)
            return;
        usleep(100 * 1000);
    }
    fprintf(stderr, "[mycontainer] warning: failed to remove cgroup %s: %s\n",
            path, strerror(errno));
}
