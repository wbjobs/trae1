#include "mycontainer.h"

struct child_config {
    struct container_args *args;
};

static volatile sig_atomic_t g_fwd_signal = 0;
static volatile sig_atomic_t g_main_child = 0;

static void signal_forward_handler(int sig)
{
    g_fwd_signal = sig;
    if (g_main_child > 0)
        kill(-(pid_t)g_main_child, sig);
}

static void reap_all_children(void)
{
    for (;;) {
        pid_t p = waitpid(-1, NULL, WNOHANG);
        if (p <= 0)
            break;
    }
}

static int init_loop(pid_t main_child)
{
    g_main_child = (sig_atomic_t)main_child;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_forward_handler;
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

            reap_all_children();

            break;
        }
    }

    return main_exit_code;
}

static int child_fn(void *arg)
{
    struct child_config *cfg = (struct child_config *)arg;
    struct container_args *a = cfg->args;

    if (setup_namespaces() < 0)
        return 1;

    if (setup_hostname() < 0)
        return 1;

    if (setup_mounts(a->image_dir) < 0)
        return 1;

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

        execvp(a->argv[0], a->argv);
        fprintf(stderr, "[mycontainer] execvp %s: %s\n",
                a->argv[0], strerror(errno));
        _exit(127);
    }

    if (setpgid(pid, pid) < 0) {
        perror("setpgid");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return 1;
    }

    return init_loop(pid);
}

int run_container(struct container_args *args)
{
    static char stack[STACK_SIZE];
    struct child_config cfg = { .args = args };

    int clone_flags = CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS |
                      CLONE_NEWUTS | SIGCHLD;

    pid_t pid = clone(child_fn, stack + sizeof(stack),
                      clone_flags, &cfg);
    if (pid < 0)
        die("clone");

    args->child_pid = pid;

    struct container_state state;
    memset(&state, 0, sizeof(state));
    strncpy(state.container_id, args->container_id,
            sizeof(state.container_id) - 1);
    strncpy(state.image_dir, args->image_dir,
            sizeof(state.image_dir) - 1);
    strncpy(state.cgroup_name, args->cgroup_name,
            sizeof(state.cgroup_name) - 1);
    strncpy(state.cmd, args->cmd, sizeof(state.cmd) - 1);
    state.argc = args->argc;
    for (int i = 0; i < args->argc && i < 64; ++i)
        strncpy(state.argv[i], args->argv[i],
                sizeof(state.argv[0]) - 1);
    state.init_pid = pid;
    state.checkpointed = 0;
    save_container_state(&state);

    printf("[mycontainer] started container '%s' (init pid=%d, cgroup=%s)\n",
           args->container_id, pid, args->cgroup_name);

    if (setup_cgroup(pid, args->cgroup_name) < 0) {
        fprintf(stderr, "[mycontainer] warning: cgroup setup failed, "
                "continuing without limits\n");
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        die("waitpid");

    cleanup_cgroup(args->cgroup_name);
    delete_container_state(args->container_id);

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
