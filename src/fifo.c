#include "fifo.h"

#include "pipe.h"
#include "utils.h"



static struct file_operations fifo_ops = {
	.readable = fifo_readable,
	.writable = fifo_writable,
	.read = fifo_read,
	.write = fifo_write,
	.lseekable = NULL,
	.lseek = NULL,
};

int mkfifo(const char *pathname, int mode)
{
	mkfile(pathname, mode, S_IFIFO);
	return 0;
}

int
fifo_init(int fd, int driver_pid, struct file *files[],
          struct memory_pool *memory_pool, struct event_monitor *monitor)
{
    struct pipe_ringbuffer *pipe;

    pipe = memory_pool_alloc(memory_pool, sizeof(struct pipe_ringbuffer));

    if (!pipe)
        return -1;

    pipe->start = 0;
    pipe->end = 0;
	pipe->file.ops = &fifo_ops;
    files[fd] = &pipe->file;

    pipe->read_event = event_monitor_find_free(monitor);
    event_monitor_register(monitor, pipe->read_event, pipe_read_release, files[fd]);

    pipe->write_event = event_monitor_find_free(monitor);
    event_monitor_register(monitor, pipe->write_event, pipe_write_release, files[fd]);
    return 0;
}

int
fifo_readable (struct file *file, struct file_request *request,
               struct event_monitor *monitor)
{
	/* Trying to read too much */
	if (request->size > PIPE_BUF) {
		return FILE_ACCESS_ERROR;
	}

	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	if ((size_t)PIPE_LEN(*pipe) < request->size) {
		/* Trying to read more than there is: block */
		event_monitor_block(monitor, pipe->read_event, request->task);
		return FILE_ACCESS_BLOCK;
	}
	return FILE_ACCESS_ACCEPT;
}

int
fifo_writable (struct file *file, struct file_request *request,
               struct event_monitor *monitor)
{
	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	/* If the write would be non-atomic */
	if (request->size > PIPE_BUF) {
		return FILE_ACCESS_ERROR;
	}
	/* Preserve 1 byte to distiguish empty or full */
	if ((size_t)PIPE_BUF - PIPE_LEN(*pipe) - 1 < request->size) {
		/* Trying to write more than we have space for: block */
	    event_monitor_block(monitor, pipe->write_event, request->task);
		return FILE_ACCESS_BLOCK;
	}
	return FILE_ACCESS_ACCEPT;
}

int
fifo_read (struct file *file, struct file_request *request,
               struct event_monitor *monitor)
{
	size_t i;
	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	/* Copy data into buf */
	for (i = 0; i < request->size; i++) {
		PIPE_POP(*pipe, request->buf[i]);
	}

    /* Prepared to write */
	event_monitor_release(monitor, pipe->write_event);
	return request->size;
}

int
fifo_write (struct file *file, struct file_request *request,
               struct event_monitor *monitor)
{
	size_t i;
	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	/* Copy data into pipe */
	for (i = 0; i < request->size; i++)
		PIPE_PUSH(*pipe, request->buf[i]);

    /* Prepared to read */
	event_monitor_release(monitor, pipe->read_event);
	return request->size;
}

