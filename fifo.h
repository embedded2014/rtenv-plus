#ifndef FIFO_H
#define FIFO_H

#include "file.h"
#include "memory-pool.h"

int mkfifo(const char *pathname, int mode);
int fifo_init(struct file **file_ptr, struct memory_pool *memory_pool);
int fifo_readable (struct file *file, char *buf, size_t size,
                   struct task_control_block *task);
int fifo_writable (struct file *file, char *buf, size_t size,
                   struct task_control_block *task);
int fifo_read (struct file *file, char *buf, size_t size,
               struct task_control_block *task);
int fifo_write (struct file *file, char *buf, size_t size,
                struct task_control_block *task);

#endif
