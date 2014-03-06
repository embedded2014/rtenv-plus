#ifndef FILE_H
#define FILE_H

#include "stddef.h"
#include "task.h"
#include "event-monitor.h"
#include "memory-pool.h"

/* file types */
#define S_IFIFO 1
#define S_IMSGQ 2
#define S_IFBLK 6
#define S_IFREG 010

/* file flags */
#define O_CREAT 4

/* seek whence */
#define SEEK_SET 1
#define SEEK_CUR 2
#define SEEK_END 3

#define FILE_ACCESS_ACCEPT 1
#define FILE_ACCESS_BLOCK  0
#define FILE_ACCESS_ERROR -1

#define FILE_EVENT_READ(fd) ((fd) * 2)
#define FILE_EVENT_WRITE(fd) ((fd) * 2 + 1)

#define FILE_EVENT_IS_READ(event) ((event) % 2 == 0)

struct file_request {
    struct task_control_block *task;
    char *buf;
    int size;
    int whence;
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
    int (*lseekable)(struct file*, struct file_request*, struct event_monitor *);
    int (*lseek)(struct file*, struct file_request*, struct event_monitor *);
};

int mkfile(const char *pathname, int mode, int dev);
int open(const char *pathname, int flags);

int file_read(struct file *file, struct file_request *request,
              struct event_monitor *monitor);
int file_write(struct file *file, struct file_request *request,
               struct event_monitor *monitor);
int file_mknod(int fd, int driver_pid, struct file *files[], int dev,
               struct memory_pool *memory_pool,
               struct event_monitor *event_monitor);
int file_lseek(struct file *file, struct file_request *request,
               struct event_monitor *monitor);

#endif
