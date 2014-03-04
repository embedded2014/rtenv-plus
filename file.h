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

#define FILE_ACCESS_ACCEPT 1
#define FILE_ACCESS_BLOCK  0
#define FILE_ACCESS_ERROR -1

#define FILE_EVENT_READ(fd) ((fd) * 2)
#define FILE_EVENT_WRITE(fd) ((fd) * 2 + 1)

#define FILE_EVENT_IS_READ(event) ((event) % 2 == 0)

struct file_request {
    struct task_control_block *task;
    char *buf;
    size_t size;
};

struct file {
    int fd;
    struct file_operations *ops;
};

struct file_operations {
    int (*readable)(struct file*, struct file_request*, struct event_monitor *);
    int (*writable)(struct file*, struct file_request*, struct event_monitor *);
    int (*read)(struct file*, struct file_request*, struct event_monitor *);
    int (*write)(struct file*, struct file_request*, struct event_monitor *);
};

int mkfile(const char *pathname, int mode, int dev);
int open(const char *pathname, int flags);

#endif
