#include "mycontainer.h"

int setup_namespaces(void)
{
    if (unshare(CLONE_NEWUTS) < 0) {
        perror("unshare(UTS)");
        return -1;
    }
    return 0;
}
