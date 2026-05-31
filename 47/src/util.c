#include "mycontainer.h"

void die(const char *msg)
{
    fprintf(stderr, "[mycontainer] error: %s: %s\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s <command> [options]\n"
            "\n"
            "Lightweight container runtime using Linux namespaces and cgroup v2.\n"
            "\n"
            "Commands:\n"
            "  run [--name <id>] <image_dir> <cmd> [args...]\n"
            "      Start a new isolated container.\n"
            "      --name <id>   Optional container ID (default: mycontainer-<pid>).\n"
            "      image_dir     Path to the rootfs to use as container root.\n"
            "      cmd           Command to run inside the container.\n"
            "\n"
            "  checkpoint <container_id>\n"
            "      Snapshot a running container to disk using CRIU.\n"
            "      The container continues running after checkpoint.\n"
            "\n"
            "  restore <container_id>\n"
            "      Restore a container from a previously saved checkpoint.\n"
            "      Network connections may break (TCP requires reconnect).\n"
            "\n"
            "Default resource limits:\n"
            "  - CPU:    1 core (quota 100ms / period 100ms)\n"
            "  - Memory: 256MB\n"
            "\n"
            "State directory: " STATE_DIR "\n",
            prog);
}
