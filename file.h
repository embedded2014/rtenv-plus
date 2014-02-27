#ifndef FILE_H
#define FILE_H

#include "task.h"

/* file types */
#define S_IFIFO 1
#define S_IMSGQ 2

/* file flags */
#define O_CREAT 4

struct file {
	int (*readable) (struct file*, struct task_control_block*);
	int (*writable) (struct file*, struct task_control_block*);
	int (*read) (struct file*, struct task_control_block*);
	int (*write) (struct file*, struct task_control_block*);
};

int mkfile(const char *pathname, int mode, int dev);
int open(const char *pathname, int flags);

#endif
