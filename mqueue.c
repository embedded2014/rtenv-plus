#include "mqueue.h"

#include <stddef.h>
#include "utils.h"
#include "pipe.h"



static struct file_operations mq_ops = {
	.readable = mq_readable,
	.writable = mq_writable,
	.read = mq_read,
	.write = mq_write,
	.lseekable = NULL,
	.lseek = NULL,
};

int mq_open(const char *name, int oflag)
{
	if (oflag & O_CREAT)
		mkfile(name, 0, S_IMSGQ);
	return open(name, 0);
}

int
mq_init(int fd, int driver_pid, struct file *files[],
        struct memory_pool *memory_pool, struct event_monitor *monitor)
{
    struct pipe_ringbuffer *pipe;

    pipe = memory_pool_alloc(memory_pool, sizeof(struct pipe_ringbuffer));

    if (!pipe)
        return -1;

    pipe->start = 0;
    pipe->end = 0;
	pipe->file.ops = &mq_ops;
    files[fd] = &pipe->file;

    pipe->read_event = event_monitor_find_free(monitor);
    event_monitor_register(monitor, pipe->read_event, pipe_read_release, files[fd]);

    pipe->write_event = event_monitor_find_free(monitor);
    event_monitor_register(monitor, pipe->write_event, pipe_write_release, files[fd]);
    return 0;
}

int
mq_readable (struct file *file, struct file_request *request,
             struct event_monitor *monitor)
{
	size_t msg_len;

	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	/* Trying to read too much */
	if ((size_t)PIPE_LEN(*pipe) < sizeof(size_t)) {
		/* Nothing to read */
	    event_monitor_block(monitor, pipe->read_event, request->task);
		return FILE_ACCESS_BLOCK;
	}

	PIPE_PEEK(*pipe, msg_len, 4);

	if (msg_len > request->size) {
		/* Trying to read more than buffer size */
		return FILE_ACCESS_ERROR;
	}
	return FILE_ACCESS_ACCEPT;
}

int
mq_writable (struct file *file, struct file_request *request,
             struct event_monitor *monitor)
{
	size_t total_len = sizeof(size_t) + request->size;
	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	/* If the write would be non-atomic */
	if (total_len > PIPE_BUF) {
		return FILE_ACCESS_ERROR;
	}
	/* Preserve 1 byte to distiguish empty or full */
	if ((size_t)PIPE_BUF - PIPE_LEN(*pipe) - 1 < total_len) {
		/* Trying to write more than we have space for: block */
	    event_monitor_block(monitor, pipe->write_event, request->task);
		return FILE_ACCESS_BLOCK;
	}
	return FILE_ACCESS_ACCEPT;
}

int
mq_read (struct file *file, struct file_request *request,
         struct event_monitor *monitor)
{
	size_t msg_len;
	size_t i;
	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	/* Get length */
	for (i = 0; i < 4; i++) {
		PIPE_POP(*pipe, *(((char*)&msg_len)+i));
	}
	/* Copy data into buf */
	for (i = 0; i < msg_len; i++) {
		PIPE_POP(*pipe, request->buf[i]);
	}

    /* Prepared to write */
	event_monitor_release(monitor, pipe->write_event);
	return msg_len;
}

int
mq_write (struct file *file, struct file_request *request,
          struct event_monitor *monitor)
{
	size_t i;
	struct pipe_ringbuffer *pipe =
	    container_of(file, struct pipe_ringbuffer, file);

	/* Copy count into pipe */
	for (i = 0; i < sizeof(size_t); i++)
		PIPE_PUSH(*pipe,*(((char*)&request->size)+i));
	/* Copy data into pipe */
	for (i = 0; i < request->size; i++)
		PIPE_PUSH(*pipe,request->buf[i]);

    /* Prepared to read */
	event_monitor_release(monitor, pipe->read_event);
	return request->size;
}

