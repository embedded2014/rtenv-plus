#ifndef MQUEUE_H
#define MQUEUE_H

#include "file.h"
#include "memory-pool.h"

int mq_init(int fd, int driver_pid, struct file *files[],
            struct memory_pool *memory_pool, struct event_monitor *monitor);
int mq_open(const char *name, int oflag);
int mq_readable (struct file *file, struct file_request *request,
                 struct event_monitor *monitor);
int mq_writable (struct file *file, struct file_request *request,
                 struct event_monitor *monitor);
int mq_read (struct file *file, struct file_request *request,
             struct event_monitor *monitor);
int mq_write (struct file *file, struct file_request *request,
              struct event_monitor *monitor);

#endif
