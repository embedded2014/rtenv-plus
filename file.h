#ifndef FILE_H
#define FILE_H

#include "stddef.h"
#include "task.h"

/* file types */
#define S_IFIFO 1
#define S_IMSGQ 2
#define S_IFBLK 6

/* file flags */
#define O_CREAT 4

struct file {
	int (*readable) (struct file*, char *, size_t, struct task_control_block*);
	int (*writable) (struct file*, char *, size_t, struct task_control_block*);
	int (*read) (struct file*, char *, size_t, struct task_control_block*);
	int (*write) (struct file*, char *, size_t, struct task_control_block*);
};

int mkfile(const char *pathname, int mode, int dev);
int open(const char *pathname, int flags);

#endif
