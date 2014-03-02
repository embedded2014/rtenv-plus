#ifndef FILE_H
#define FILE_H

#include "stddef.h"
#include "task.h"
#include "event-monitor.h"

/* file types */
#define S_IFIFO 1
#define S_IMSGQ 2
#define S_IFBLK 6

/* file flags */
#define O_CREAT 4

struct file_request {
    struct task_control_block *task;
    char *buf;
    size_t size;
};

struct file {
    int fd;
    int (*readable)(struct file*, struct file_request*, struct event_monitor *);
    int (*writable)(struct file*, struct file_request*, struct event_monitor *);
    int (*read)(struct file*, struct file_request*, struct event_monitor *);
    int (*write)(struct file*, struct file_request*, struct event_monitor *);
};

int mkfile(const char *pathname, int mode, int dev);
int open(const char *pathname, int flags);

#endif
