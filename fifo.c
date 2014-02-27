#include "fifo.h"

#include "pipe.h"
#include "utils.h"



int mkfifo(const char *pathname, int mode)
{
	mkfile(pathname, mode, S_IFIFO);
	return 0;
}

int
fifo_init(int fd, int driver_pid, struct file *files[],
          struct memory_pool *memory_pool)
{
    struct pipe_ringbuffer *pipe;

    pipe = memory_pool_alloc(memory_pool, sizeof(struct pipe_ringbuffer));

    if (!pipe)
        return -1;

    pipe->start = 0;
    pipe->end = 0;
	pipe->file.readable = fifo_readable;
	pipe->file.writable = fifo_writable;
	pipe->file.read = fifo_read;
	pipe->file.write = fifo_write;
    files[fd] = &pipe->file;
    return 0;
}

int
fifo_readable (struct file *file, char *buf, size_t size,
			   struct task_control_block *task)
{
	/* Trying to read too much */
	if (task->stack->r2 > PIPE_BUF) {
		task->stack->r0 = -1;
		return 0;
	}

	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	if ((size_t)PIPE_LEN(*pipe) < task->stack->r2) {
		/* Trying to read more than there is: block */
		task->status = TASK_WAIT_READ;
		return 0;
	}
	return 1;
}

int
fifo_writable (struct file *file, char *buf, size_t size,
			   struct task_control_block *task)
{
	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	/* If the write would be non-atomic */
	if (task->stack->r2 > PIPE_BUF) {
		task->stack->r0 = -1;
		return 0;
	}
	/* Preserve 1 byte to distiguish empty or full */
	if ((size_t)PIPE_BUF - PIPE_LEN(*pipe) - 1 < task->stack->r2) {
		/* Trying to write more than we have space for: block */
		task->status = TASK_WAIT_WRITE;
		return 0;
	}
	return 1;
}

int
fifo_read (struct file *file, char *buf, size_t size,
		   struct task_control_block *task)
{
	size_t i;
	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	/* Copy data into buf */
	for (i = 0; i < task->stack->r2; i++) {
		PIPE_POP(*pipe, buf[i]);
	}
	return task->stack->r2;
}

int
fifo_write (struct file *file, char *buf, size_t size,
			struct task_control_block *task)
{
	size_t i;
	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	/* Copy data into pipe */
	for (i = 0; i < task->stack->r2; i++)
		PIPE_PUSH(*pipe,buf[i]);
	return task->stack->r2;
}

