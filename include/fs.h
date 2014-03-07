#ifndef FS_H
#define FS_H

#include "kconfig.h"

#define FS_CMD_OPEN 1
#define FS_CMD_READ 2
#define FS_CMD_WRITE 3
#define FS_CMD_SEEK 4

struct fs_request {
    int cmd;
    int from;
    int device;
    int target;
    char path[PATH_MAX];
    int size;
    int pos;
};

#endif
