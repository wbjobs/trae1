#include "mycontainer.h"
#include <sys/syscall.h>
#include <limits.h>

int container_state_dir(const char *container_id, char *buf, size_t len)
{
    return snprintf(buf, len, "%s/%s", STATE_DIR, container_id);
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        return 0;
    }
    if (mkdir(path, 0755) < 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int write_str(int fd, const char *key, const char *val)
{
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "%s=%s\n", key, val);
    return write(fd, buf, n) == n ? 0 : -1;
}

static int write_int(int fd, const char *key, long val)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%ld", val);
    return write_str(fd, key, buf);
}

int save_container_state(const struct container_state *state)
{
    char dir[512];
    container_state_dir(state->container_id, dir, sizeof(dir));

    if (ensure_dir(STATE_DIR) < 0) {
        fprintf(stderr, "[mycontainer] cannot create %s: %s\n",
                STATE_DIR, strerror(errno));
        return -1;
    }
    if (ensure_dir(dir) < 0) {
        fprintf(stderr, "[mycontainer] cannot create %s: %s\n",
                dir, strerror(errno));
        return -1;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, STATE_FILE);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "[mycontainer] cannot write %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    write_str(fd, "container_id", state->container_id);
    write_str(fd, "image_dir", state->image_dir);
    write_str(fd, "cgroup_name", state->cgroup_name);
    write_str(fd, "cmd", state->cmd);
    write_int(fd, "init_pid", (long)state->init_pid);
    write_int(fd, "argc", state->argc);
    write_int(fd, "checkpointed", state->checkpointed);

    for (int i = 0; i < state->argc && i < 64; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "argv[%d]", i);
        write_str(fd, key, state->argv[i]);
    }

    close(fd);
    return 0;
}

static int read_line(int fd, char *buf, size_t bufsz)
{
    size_t i = 0;
    char c;
    while (i < bufsz - 1) {
        ssize_t n = read(fd, &c, 1);
        if (n <= 0)
            break;
        if (c == '\n')
            break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i > 0 ? 0 : -1;
}

int load_container_state(const char *container_id,
                         struct container_state *state)
{
    char dir[512];
    container_state_dir(container_id, dir, sizeof(dir));

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, STATE_FILE);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[mycontainer] cannot read %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    memset(state, 0, sizeof(*state));

    char line[1024];
    while (read_line(fd, line, sizeof(line)) == 0) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "container_id") == 0)
            strncpy(state->container_id, val, CONTAINER_ID_LEN - 1);
        else if (strcmp(key, "image_dir") == 0)
            strncpy(state->image_dir, val, sizeof(state->image_dir) - 1);
        else if (strcmp(key, "cgroup_name") == 0)
            strncpy(state->cgroup_name, val, sizeof(state->cgroup_name) - 1);
        else if (strcmp(key, "cmd") == 0)
            strncpy(state->cmd, val, sizeof(state->cmd) - 1);
        else if (strcmp(key, "init_pid") == 0)
            state->init_pid = (pid_t)atol(val);
        else if (strcmp(key, "argc") == 0)
            state->argc = atoi(val);
        else if (strcmp(key, "checkpointed") == 0)
            state->checkpointed = atoi(val);
        else if (strncmp(key, "argv[", 5) == 0) {
            int idx = atoi(key + 5);
            if (idx >= 0 && idx < 64)
                strncpy(state->argv[idx], val,
                        sizeof(state->argv[0]) - 1);
        }
    }

    close(fd);
    return 0;
}

int delete_container_state(const char *container_id)
{
    char dir[512];
    container_state_dir(container_id, dir, sizeof(dir));

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, STATE_FILE);
    unlink(path);

    char img_dir[512];
    snprintf(img_dir, sizeof(img_dir), "%s/%s", dir, CRIU_IMG_DIR);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' 2>/dev/null", img_dir);
    (void)system(cmd);

    rmdir(dir);
    return 0;
}

static int find_criu_binary(char *buf, size_t len)
{
    const char *paths[] = {
        "/usr/bin/criu",
        "/usr/sbin/criu",
        "/usr/local/bin/criu",
        "/sbin/criu",
        NULL
    };

    for (int i = 0; paths[i]; ++i) {
        if (access(paths[i], X_OK) == 0) {
            strncpy(buf, paths[i], len - 1);
            buf[len - 1] = '\0';
            return 0;
        }
    }

    char *path = getenv("PATH");
    if (!path)
        return -1;

    char *saveptr;
    char *dir = strtok_r(path, ":", &saveptr);
    while (dir) {
        snprintf(buf, len, "%s/criu", dir);
        if (access(buf, X_OK) == 0)
            return 0;
        dir = strtok_r(NULL, ":", &saveptr);
    }

    return -1;
}

static int run_criu_dump(pid_t init_pid, const char *img_dir)
{
    char criu[256];
    if (find_criu_binary(criu, sizeof(criu)) < 0) {
        fprintf(stderr,
                "[mycontainer] error: CRIU binary not found. "
                "Install criu (apt install criu or equivalent).\n");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0) {
        char pidbuf[32];
        snprintf(pidbuf, sizeof(pidbuf), "%d", init_pid);

        execl(criu, criu, "dump",
              "--tree", pidbuf,
              "--images-dir", img_dir,
              "--leave-running",
              "--tcp-established",
              "--ext-unix-sk",
              "--file-locks",
              "--shell-job",
              "--evasive-devices",
              NULL);
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0)
        return -1;

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;

    fprintf(stderr, "[mycontainer] criu dump failed");
    if (WIFEXITED(status))
        fprintf(stderr, " (exit code %d)", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        fprintf(stderr, " (signal %d)", WTERMSIG(status));
    fprintf(stderr, "\n");
    return -1;
}

int checkpoint_container(const char *container_id)
{
    struct container_state state;
    if (load_container_state(container_id, &state) < 0) {
        fprintf(stderr, "[mycontainer] container '%s' not found\n",
                container_id);
        return -1;
    }

    if (state.init_pid <= 0) {
        fprintf(stderr, "[mycontainer] invalid init pid in state\n");
        return -1;
    }

    if (kill(state.init_pid, 0) < 0) {
        fprintf(stderr,
                "[mycontainer] container init process (pid=%d) not running: %s\n",
                state.init_pid, strerror(errno));
        return -1;
    }

    char dir[512];
    container_state_dir(container_id, dir, sizeof(dir));

    char img_dir[512];
    snprintf(img_dir, sizeof(img_dir), "%s/%s", dir, CRIU_IMG_DIR);

    if (ensure_dir(img_dir) < 0) {
        fprintf(stderr, "[mycontainer] cannot create checkpoint dir %s: %s\n",
                img_dir, strerror(errno));
        return -1;
    }

    printf("[mycontainer] checkpointing container '%s' (init pid=%d)...\n",
           container_id, state.init_pid);

    if (run_criu_dump(state.init_pid, img_dir) < 0)
        return -1;

    state.checkpointed = 1;
    save_container_state(&state);

    printf("[mycontainer] checkpoint saved to %s\n", img_dir);
    return 0;
}

struct restore_config {
    struct container_state *state;
    char criu_path[256];
    char host_img_dir[512];
};

static volatile sig_atomic_t g_r_fwd_signal = 0;
static volatile sig_atomic_t g_r_main_child = 0;

static void restore_signal_forward_handler(int sig)
{
    g_r_fwd_signal = sig;
    if (g_r_main_child > 0)
        kill(-(pid_t)g_r_main_child, sig);
}

static void restore_reap_all(void)
{
    for (;;) {
        pid_t p = waitpid(-1, NULL, WNOHANG);
        if (p <= 0)
            break;
    }
}

static int restore_init_loop(pid_t main_child)
{
    g_r_main_child = (sig_atomic_t)main_child;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = restore_signal_forward_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    int main_exit_code = 1;
    int main_exited = 0;

    for (;;) {
        int status;
        pid_t reaped = waitpid(-1, &status, 0);

        if (reaped < 0) {
            if (errno == EINTR) {
                if (!main_exited)
                    continue;
                break;
            }
            if (errno == ECHILD)
                break;
            break;
        }

        if (reaped == main_child) {
            if (WIFEXITED(status))
                main_exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                main_exit_code = 128 + WTERMSIG(status);
            main_exited = 1;

            kill(-main_child, SIGTERM);

            for (int i = 0; i < 100; ++i) {
                pid_t p = waitpid(-1, NULL, WNOHANG);
                if (p < 0) {
                    if (errno == ECHILD)
                        break;
                    continue;
                }
                if (p == 0) {
                    usleep(100 * 1000);
                    continue;
                }
            }

            kill(-main_child, SIGKILL);
            restore_reap_all();
            break;
        }
    }

    return main_exit_code;
}

static int restore_child_fn(void *arg)
{
    struct restore_config *cfg = (struct restore_config *)arg;
    struct container_state *st = cfg->state;

    if (setup_namespaces() < 0)
        return 1;

    if (setup_hostname() < 0)
        return 1;

    if (setup_mounts(st->image_dir) < 0)
        return 1;

    if (mkdir("/tmp", 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "[mycontainer] mkdir /tmp: %s\n", strerror(errno));
    }
    if (mkdir("/tmp/criu", 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "[mycontainer] mkdir /tmp/criu: %s\n", strerror(errno));
    }

    char oldroot_img[512];
    snprintf(oldroot_img, sizeof(oldroot_img),
             "/.oldroot%s", cfg->host_img_dir);

    if (mount(oldroot_img, "/tmp/criu", NULL, MS_BIND, NULL) < 0) {
        fprintf(stderr, "[mycontainer] bind mount checkpoint dir failed: %s\n",
                strerror(errno));
    }

    char oldroot_criu[512];
    snprintf(oldroot_criu, sizeof(oldroot_criu),
             "/.oldroot%s", cfg->criu_path);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        struct sigaction sa_dfl;
        memset(&sa_dfl, 0, sizeof(sa_dfl));
        sa_dfl.sa_handler = SIG_DFL;
        sigemptyset(&sa_dfl.sa_mask);
        sigaction(SIGTERM, &sa_dfl, NULL);
        sigaction(SIGINT, &sa_dfl, NULL);
        sigaction(SIGHUP, &sa_dfl, NULL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);

        execl(oldroot_criu, "criu", "restore",
              "--images-dir", "/tmp/criu",
              "--restore-detached",
              "--tcp-established",
              "--ext-unix-sk",
              "--file-locks",
              "--shell-job",
              "--evasive-devices",
              NULL);
        _exit(127);
    }

    int criu_status;
    if (waitpid(pid, &criu_status, 0) < 0) {
        perror("waitpid criu");
        return 1;
    }

    if (umount2("/.oldroot", MNT_DETACH) < 0) {
        fprintf(stderr, "[mycontainer] umount oldroot: %s\n", strerror(errno));
    }

    if (!(WIFEXITED(criu_status) && WEXITSTATUS(criu_status) == 0)) {
        fprintf(stderr, "[mycontainer] criu restore failed");
        if (WIFEXITED(criu_status))
            fprintf(stderr, " (exit code %d)", WEXITSTATUS(criu_status));
        else if (WIFSIGNALED(criu_status))
            fprintf(stderr, " (signal %d)", WTERMSIG(criu_status));
        fprintf(stderr, "\n");
        return 1;
    }

    pid_t restored_pid = 0;
    int fd = open("/tmp/criu/" CRIU_RESTORE_PID_FILE, O_RDONLY);
    if (fd >= 0) {
        char buf[32];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            restored_pid = (pid_t)atoi(buf);
        }
        close(fd);
    }

    if (restored_pid <= 0) {
        fprintf(stderr,
                "[mycontainer] cannot read restore.pid, "
                "cannot forward signals\n");
    }

    umount2("/tmp/criu", MNT_DETACH);
    rmdir("/tmp/criu");

    if (restored_pid > 0) {
        if (setpgid(restored_pid, restored_pid) < 0) {
            fprintf(stderr, "[mycontainer] setpgid %d: %s\n",
                    restored_pid, strerror(errno));
        }
        return restore_init_loop(restored_pid);
    }

    for (;;)
        pause();
    return 1;
}

int restore_container(const char *container_id)
{
    struct container_state state;
    if (load_container_state(container_id, &state) < 0) {
        fprintf(stderr, "[mycontainer] container '%s' not found\n",
                container_id);
        return -1;
    }

    if (!state.checkpointed) {
        fprintf(stderr,
                "[mycontainer] container '%s' has no checkpoint\n",
                container_id);
        return -1;
    }

    struct restore_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.state = &state;

    if (find_criu_binary(cfg.criu_path, sizeof(cfg.criu_path)) < 0) {
        fprintf(stderr,
                "[mycontainer] error: CRIU binary not found. "
                "Install criu (apt install criu or equivalent).\n");
        return -1;
    }

    container_state_dir(container_id, cfg.host_img_dir,
                        sizeof(cfg.host_img_dir));
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s/%s", cfg.host_img_dir, CRIU_IMG_DIR);
    strncpy(cfg.host_img_dir, tmp, sizeof(cfg.host_img_dir) - 1);

    char cgroup_name[128];
    snprintf(cgroup_name, sizeof(cgroup_name),
             "%s%ld", CGROUP_NAME_PREFIX, (long)getpid());

    static char stack[STACK_SIZE];
    int clone_flags = CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS |
                      CLONE_NEWUTS | SIGCHLD;

    pid_t pid = clone(restore_child_fn, stack + sizeof(stack),
                      clone_flags, &cfg);
    if (pid < 0)
        die("clone");

    printf("[mycontainer] restoring container '%s' (init pid=%d, cgroup=%s)\n",
           container_id, pid, cgroup_name);

    if (setup_cgroup(pid, cgroup_name) < 0) {
        fprintf(stderr, "[mycontainer] warning: cgroup setup failed, "
                "continuing without limits\n");
    }

    state.init_pid = pid;
    strncpy(state.cgroup_name, cgroup_name,
            sizeof(state.cgroup_name) - 1);
    state.checkpointed = 0;
    save_container_state(&state);

    int status;
    if (waitpid(pid, &status, 0) < 0)
        die("waitpid");

    cleanup_cgroup(cgroup_name);
    delete_container_state(container_id);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        printf("[mycontainer] container exited with status %d\n", code);
        return code;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("[mycontainer] container killed by signal %d\n", sig);
        return 128 + sig;
    }
    return 0;
}
