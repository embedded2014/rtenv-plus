#ifndef BLOCK_H
#define BLOCK_H

#include "file.h"
#include "memory-pool.h"

#define BLOCK_CMD_READ 1
#define BLOCK_CMD_WRITE 2

struct block {
    struct file file;
    int driver_pid;
    struct file *driver_file;

    /* request */
    int request_pid;
    int buzy;
    int pos;
    char buf[BLOCK_BUF];

    /* response */
    size_t transfer_len;
};

struct block_request {
    int cmd;
    int task;
    int fd;
    size_t size;
    int pos;
};

int block_init(int fd, int driver_pid, struct file *files[],
               struct memory_pool *memory_pool);
int block_response(int fd, char *buf, int len);
int block_readable (struct file *file, struct file_request *request,
                    struct event_monitor *monitor);
int block_writable (struct file *file, struct file_request *request,
                    struct event_monitor *monitor);
int block_read (struct file *file, struct file_request *request,
                struct event_monitor *monitor);
int block_write (struct file *file, struct file_request *request,
                 struct event_monitor *monitor);

#endif
