#include "mycontainer.h"

static void generate_container_id(char *buf, size_t len)
{
    snprintf(buf, len, "mycontainer-%ld", (long)getpid());
}

static int cmd_run(int argc, char **argv)
{
    struct container_args args;
    memset(&args, 0, sizeof(args));

    int i = 0;
    while (i < argc) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            strncpy(args.container_id, argv[i + 1], CONTAINER_ID_LEN - 1);
            i += 2;
        } else {
            break;
        }
    }

    if (i >= argc) {
        fprintf(stderr, "error: 'run' requires <image_dir> and <cmd>\n");
        usage(argv[-1]);
        return EXIT_FAILURE;
    }

    args.image_dir = argv[i++];

    if (i >= argc) {
        fprintf(stderr, "error: 'run' requires <cmd>\n");
        usage(argv[-1]);
        return EXIT_FAILURE;
    }

    int n = 0;
    while (i < argc && n < 63) {
        args.argv[n++] = argv[i++];
    }
    args.argv[n] = NULL;
    args.argc    = n;
    args.cmd     = args.argv[0];

    if (args.container_id[0] == '\0')
        generate_container_id(args.container_id, sizeof(args.container_id));

    snprintf(args.cgroup_name, sizeof(args.cgroup_name),
             "%s%ld", CGROUP_NAME_PREFIX, (long)getpid());

    return run_container(&args);
}

static int cmd_checkpoint(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "error: 'checkpoint' requires <container_id>\n");
        return EXIT_FAILURE;
    }
    if (checkpoint_container(argv[0]) < 0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static int cmd_restore(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "error: 'restore' requires <container_id>\n");
        return EXIT_FAILURE;
    }
    if (restore_container(argv[0]) < 0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "run") == 0)
        return cmd_run(argc - 2, argv + 2);
    if (strcmp(subcmd, "checkpoint") == 0)
        return cmd_checkpoint(argc - 2, argv + 2);
    if (strcmp(subcmd, "restore") == 0)
        return cmd_restore(argc - 2, argv + 2);

    fprintf(stderr, "unknown subcommand: %s\n", subcmd);
    usage(argv[0]);
    return EXIT_FAILURE;
}
