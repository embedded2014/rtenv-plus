#ifndef REGFILE_H
#define REGFILE_H

#include "file.h"
#include "fs.h"
#include "memory-pool.h"

struct regfile {
    struct file file;
    int driver_pid;
    struct file *driver_file;
    int event;

    /* request */
    int request_pid;
    int buzy;
    int pos;
    char buf[REGFILE_BUF];

    /* response */
    int transfer_len;
};

int regfile_init(int fd, int driver_pid, struct file *files[],
                 struct memory_pool *memory_pool, struct event_monitor *monitor);
int regfile_response(int fd, char *buf, int len);
int regfile_readable (struct file *file, struct file_request *request,
                      struct event_monitor *monitor);
int regfile_writable (struct file *file, struct file_request *request,
                      struct event_monitor *monitor);
int regfile_read (struct file *file, struct file_request *request,
                  struct event_monitor *monitor);
int regfile_write (struct file *file, struct file_request *request,
                   struct event_monitor *monitor);
int regfile_lseekable (struct file *file, struct file_request *request,
                       struct event_monitor *monitor);
int regfile_lseek (struct file *file, struct file_request *request,
                   struct event_monitor *monitor);

#endif
