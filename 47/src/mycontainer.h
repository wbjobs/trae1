#ifndef MYCONTAINER_H
#define MYCONTAINER_H

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <libgen.h>

#define STACK_SIZE            (1024 * 1024)
#define CGROUP_NAME_PREFIX    "mycontainer-"
#define DEFAULT_CPU_QUOTA      100000
#define DEFAULT_CPU_PERIOD     100000
#define DEFAULT_MEM_LIMIT      (256UL * 1024 * 1024)
#define STATE_DIR              "/var/lib/mycontainer"
#define STATE_FILE             "state"
#define CRIU_IMG_DIR           "checkpoint"
#define CRIU_RESTORE_PID_FILE  "restore.pid"
#define CONTAINER_ID_LEN       128

struct container_args {
    char  container_id[CONTAINER_ID_LEN];
    char *image_dir;
    char *cmd;
    char *argv[64];
    int   argc;
    char  cgroup_name[128];
    pid_t child_pid;
};

struct container_state {
    char  container_id[CONTAINER_ID_LEN];
    char  image_dir[512];
    char  cgroup_name[128];
    char  cmd[256];
    char  argv[64][256];
    int   argc;
    pid_t init_pid;
    int   checkpointed;
};

int  run_container(struct container_args *args);
int  setup_cgroup(pid_t pid, const char *name);
void cleanup_cgroup(const char *name);
int  setup_namespaces(void);
int  setup_mounts(const char *image_dir);
int  setup_hostname(void);

int  checkpoint_container(const char *container_id);
int  restore_container(const char *container_id);

int  save_container_state(const struct container_state *state);
int  load_container_state(const char *container_id,
                          struct container_state *state);
int  delete_container_state(const char *container_id);
int  container_state_dir(const char *container_id, char *buf, size_t len);

void die(const char *msg);
void usage(const char *prog);

#endif
