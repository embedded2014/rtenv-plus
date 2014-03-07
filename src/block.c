#include "block.h"

#include "utils.h"
#include "string.h"
#include "syscall.h"
#include "file.h"

struct block_response {
    int transfer_len;
    char *buf;
};

static struct file_operations block_ops = {
	.readable = block_readable,
	.writable = block_writable,
	.read = block_read,
	.write = block_write,
	.lseekable = block_lseekable,
	.lseek = block_lseek,
};


int block_driver_readable (struct block *block, struct file_request *request,
                           struct event_monitor *monitor)
{
    if (block->buzy)
        return FILE_ACCESS_ACCEPT;
    else
        return FILE_ACCESS_ERROR;
}

int block_driver_writable (struct block *block, struct file_request *request,
                           struct event_monitor *monitor)
{
    if (block->buzy)
        return FILE_ACCESS_ACCEPT;
    else
        return FILE_ACCESS_ERROR;
}

int block_driver_lseekable (struct block *block, struct file_request *request,
                            struct event_monitor *monitor)
{
    if (block->buzy)
        return FILE_ACCESS_ACCEPT;
    else
        return FILE_ACCESS_ERROR;
}

int block_driver_read (struct block *block, struct file_request *request,
                       struct event_monitor *monitor)
{
    int size = request->size;
    if (size > BLOCK_BUF)
        size = BLOCK_BUF;

    memcpy(request->buf, block->buf, size);

    /* still buzy until driver write response */
    return size;
}

int block_driver_write (struct block *block, struct file_request *request,
                        struct event_monitor *monitor)
{
    struct block_response *response = (void *)request->buf;
    char *data_buf = response->buf;
    int len = response->transfer_len;
    if (len > BLOCK_BUF)
        len = BLOCK_BUF;

    if (len > 0) {
        memcpy(block->buf, data_buf, len);
    }
    block->transfer_len = len;
    block->buzy = 0;
	event_monitor_release(monitor, block->event);
    return len;
}

int block_driver_lseek (struct block *block, struct file_request *request,
                        struct event_monitor *monitor)
{
    block->transfer_len = request->size;
    block->buzy = 0;
	event_monitor_release(monitor, block->event);
    return request->size;
}

/*
 * Steps:
 *  1. Send request to ipc file of block driver
 *  2. Driver read data from device
 *  3. Driver set transfer_len
 *  4. Driver write data to buffer
 *  5. Get transfer_len
 *  6. Read data from buffer
 */
int block_request_readable (struct block *block, struct file_request *request,
                            struct event_monitor *monitor)
{
    struct task_control_block *task = request->task;

    if (block->request_pid == 0) {
        /* try to send request */
        struct file *driver = block->driver_file;
        int size = request->size;
        if (size > BLOCK_BUF)
            size = BLOCK_BUF;

        struct block_request block_request = {
            .cmd = BLOCK_CMD_READ,
            .task = task->pid,
            .fd = block->file.fd,
            .size = size,
            .pos = block->pos
        };

        struct file_request file_request = {
            .task = NULL,
            .buf = (char *)&block_request,
            .size = sizeof(block_request),
        };
        if (file_write(driver, &file_request, monitor) == 1) {
            block->request_pid = task->pid;
            block->buzy = 1;
        }
    }
    else if (block->request_pid == task->pid && !block->buzy) {
        return FILE_ACCESS_ACCEPT;
    }

	event_monitor_block(monitor, block->event, task);
    return FILE_ACCESS_BLOCK;
}

/*
 * Steps:
 *  1. Send request to ipc file of block driver
 *  2. Write data to buffer
 *  3. Driver read data from buffer
 *  4. Driver write data to device
 *  5. Driver set transfer_len
 *  6. Driver write empty data to buffer
 *  7. Get transfer_len
 */
int block_request_writable (struct block *block, struct file_request *request,
                            struct event_monitor *monitor)
{
    struct task_control_block *task = request->task;

    if (block->request_pid == 0) {
        /* try to send request */
        struct file *driver = block->driver_file;
        int size = request->size;
        if (size > BLOCK_BUF)
            size = BLOCK_BUF;

        struct block_request block_request = {
            .cmd = BLOCK_CMD_WRITE,
            .task = task->pid,
            .fd = block->file.fd,
            .size = size,
            .pos = block->pos
        };

        struct file_request file_request = {
            .task = NULL,
            .buf = (char *)&block_request,
            .size = sizeof(block_request),
        };

        if (file_write(driver, &file_request, monitor) == 1) {

            memcpy(block->buf, request->buf, size);

            block->request_pid = task->pid;
            block->buzy = 1;
        }
    }
    else if (block->request_pid == task->pid && !block->buzy) {
        return FILE_ACCESS_ACCEPT;
    }

	event_monitor_block(monitor, block->event, task);
    return FILE_ACCESS_BLOCK;
}

int block_request_lseekable (struct block *block, struct file_request *request,
                             struct event_monitor *monitor)
{
    struct task_control_block *task = request->task;

    if (block->request_pid == 0) {
        /* try to send request */
        struct file *driver = block->driver_file;
        int size = request->size;
        if (size > BLOCK_BUF)
            size = BLOCK_BUF;

        int pos;
        switch(request->whence) {
            case SEEK_SET:
                pos = 0;
                break;
            case SEEK_CUR:
                pos = block->pos;
                break;
            case SEEK_END:
                pos = -1;
                break;
            default:
                return FILE_ACCESS_ERROR;
        }

        struct block_request block_request = {
            .cmd = BLOCK_CMD_SEEK,
            .task = task->pid,
            .fd = block->file.fd,
            .size = request->size,
            .pos = pos,
        };

        struct file_request file_request = {
            .task = NULL,
            .buf = (char *)&block_request,
            .size = sizeof(block_request),
        };

        if (file_write(driver, &file_request, monitor) == 1) {
            block->request_pid = task->pid;
            block->buzy = 1;
        }
    }
    else if (block->request_pid == task->pid && !block->buzy) {
        return FILE_ACCESS_ACCEPT;
    }

	event_monitor_block(monitor, block->event, task);
    return FILE_ACCESS_BLOCK;
}

int block_request_read (struct block *block, struct file_request *request,
                        struct event_monitor *monitor)
{
    if (block->transfer_len > 0) {
        memcpy(request->buf, block->buf, block->transfer_len);

        block->pos += block->transfer_len;
    }

    block->request_pid = 0;
    return block->transfer_len;
}

int block_request_write (struct block *block, struct file_request *request,
                         struct event_monitor *monitor)
{
    if (block->transfer_len > 0) {
        block->pos += block->transfer_len;
    }

    block->request_pid = 0;
    return block->transfer_len;
}

int block_request_lseek (struct block *block, struct file_request *request,
                         struct event_monitor *monitor)
{
    if (block->transfer_len >= 0) {
        block->pos = block->transfer_len;
    }

    block->request_pid = 0;
    return block->transfer_len;
}

int block_event_release(struct event_monitor *monitor, int event,
                        struct task_control_block *task, void *data)
{
    struct file *file = data;
    struct file_request *request = (void*)task->stack->r0;

    switch (task->stack->r7) {
        case 0x04:
            return file_read(file, request, monitor);
        case 0x03:
            return file_write(file, request, monitor);
        case 0x0a:
            return file_lseek(file, request, monitor);
        default:
            return 0;
    }
}

int block_init(int fd, int driver_pid, struct file *files[],
               struct memory_pool *memory_pool, struct event_monitor *monitor)
{
    struct block *block;

    block = memory_pool_alloc(memory_pool, sizeof(*block));

    if (!block)
        return -1;

    block->driver_pid = driver_pid;
    block->driver_file = files[driver_pid + 3];
    block->request_pid = 0;
    block->buzy = 0;
    block->pos = 0;
	block->file.ops = &block_ops;
    block->event = event_monitor_find_free(monitor);
    files[fd] = &block->file;

    event_monitor_register(monitor, block->event, block_event_release, files[fd]);

    return 0;
}

int block_response(int fd, char *buf, int len)
{
    struct block_response response = {
        .transfer_len = len,
        .buf = buf
    };
    return write(fd, &response, sizeof(response));
}

int block_readable (struct file *file, struct file_request *request,
                    struct event_monitor *monitor)
{
    struct block *block = container_of(file, struct block, file);
    if (block->driver_pid == request->task->pid) {
        return block_driver_readable(block, request, monitor);
    }
    else {
        return block_request_readable(block, request, monitor);
    }
}

int block_writable (struct file *file, struct file_request *request,
                    struct event_monitor *monitor)
{
    struct block *block = container_of(file, struct block, file);
    if (block->driver_pid == request->task->pid) {
        return block_driver_writable(block, request, monitor);
    }
    else {
        return block_request_writable(block, request, monitor);
    }
}

int block_read (struct file *file, struct file_request *request,
                struct event_monitor *monitor)
{
    struct block *block = container_of(file, struct block, file);
    if (block->driver_pid == request->task->pid) {
        return block_driver_read(block, request, monitor);
    }
    else {
        return block_request_read(block, request, monitor);
    }
}

int block_write (struct file *file, struct file_request *request,
                 struct event_monitor *monitor)
{
    struct block *block = container_of(file, struct block, file);
    if (block->driver_pid == request->task->pid) {
        return block_driver_write(block, request, monitor);
    }
    else {
        return block_request_write(block, request, monitor);
    }
}

int block_lseekable (struct file *file, struct file_request *request,
                     struct event_monitor *monitor)
{
    struct block *block = container_of(file, struct block, file);
    if (block->driver_pid == request->task->pid) {
        return block_driver_lseekable(block, request, monitor);
    }
    else {
        return block_request_lseekable(block, request, monitor);
    }
}

int block_lseek (struct file *file, struct file_request *request,
                 struct event_monitor *monitor)
{
    struct block *block = container_of(file, struct block, file);
    if (block->driver_pid == request->task->pid) {
        return block_driver_lseek(block, request, monitor);
    }
    else {
        return block_request_lseek(block, request, monitor);
    }
}

