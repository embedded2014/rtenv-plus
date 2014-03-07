#ifndef BLOCK_H
#define BLOCK_H

#include "file.h"
#include "memory-pool.h"

#define BLOCK_CMD_READ 1
#define BLOCK_CMD_WRITE 2
#define BLOCK_CMD_SEEK 3

struct block {
    struct file file;
    int driver_pid;
    struct file *driver_file;
    int event;

    /* request */
    int request_pid;
    int buzy;
    int pos;
    char buf[BLOCK_BUF];

    /* response */
    int transfer_len;
};

struct block_request {
    int cmd;
    int task;
    int fd;
    int size;
    int pos;
};

int block_init(int fd, int driver_pid, struct file *files[],
               struct memory_pool *memory_pool, struct event_monitor *monitor);
int block_response(int fd, char *buf, int len);
int block_readable (struct file *file, struct file_request *request,
                    struct event_monitor *monitor);
int block_writable (struct file *file, struct file_request *request,
                    struct event_monitor *monitor);
int block_read (struct file *file, struct file_request *request,
                struct event_monitor *monitor);
int block_write (struct file *file, struct file_request *request,
                 struct event_monitor *monitor);
int block_lseekable (struct file *file, struct file_request *request,
                     struct event_monitor *monitor);
int block_lseek (struct file *file, struct file_request *request,
                 struct event_monitor *monitor);

#endif
