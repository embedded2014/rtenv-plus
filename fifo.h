#ifndef FIFO_H
#define FIFO_H

#include "file.h"
#include "memory-pool.h"

int mkfifo(const char *pathname, int mode);
int fifo_init(int fd, int driver_pid, struct file *files[],
              struct memory_pool *memory_pool, struct event_monitor *monitor);
int fifo_readable (struct file *file, struct file_request *request,
                   struct event_monitor *monitor);
int fifo_writable (struct file *file, struct file_request *request,
                   struct event_monitor *monitor);
int fifo_read (struct file *file, struct file_request *request,
               struct event_monitor *monitor);
int fifo_write (struct file *file, struct file_request *request,
                struct event_monitor *monitor);

#endif
