#include "mqueue.h"

#include <stddef.h>
#include "utils.h"
#include "pipe.h"



int mq_open(const char *name, int oflag)
{
	if (oflag & O_CREAT)
		mkfile(name, 0, S_IMSGQ);
	return open(name, 0);
}

int
mq_init(int fd, int driver_pid, struct file *files[],
        struct memory_pool *memory_pool)
{
    struct pipe_ringbuffer *pipe;

    pipe = memory_pool_alloc(memory_pool, sizeof(struct pipe_ringbuffer));

    if (!pipe)
        return -1;

    pipe->start = 0;
    pipe->end = 0;
	pipe->file.readable = mq_readable;
	pipe->file.writable = mq_writable;
	pipe->file.read = mq_read;
	pipe->file.write = mq_write;
    files[fd] = &pipe->file;
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
	return request->size;
}

