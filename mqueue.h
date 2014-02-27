#ifndef MQUEUE_H
#define MQUEUE_H

#include "file.h"
#include "memory-pool.h"

int mq_init(int fd, int driver_pid, struct file *files[],
            struct memory_pool *memory_pool);
int mq_open(const char *name, int oflag);
int mq_readable (struct file *file, char *buf, size_t size,
                 struct task_control_block *task);
int mq_writable (struct file *file, char *buf, size_t size,
                 struct task_control_block *task);
int mq_read (struct file *file, char *buf, size_t size,
             struct task_control_block *task);
int mq_write (struct file *file, char *buf, size_t size,
              struct task_control_block *task);

#endif
