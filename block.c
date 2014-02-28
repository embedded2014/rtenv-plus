#include "block.h"

#include "utils.h"
#include "string.h"
#include "syscall.h"

struct block_response {
    int transfer_len;
    char *buf;
};


int block_driver_readable (struct block *block, struct task_control_block *task)
{
    return block->buzy;
}

int block_driver_writable (struct block *block, struct task_control_block *task)
{
    return block->buzy;
}

int block_driver_read (struct block *block, struct task_control_block *task)
{
    int len = task->stack->r2;
    if (len > BLOCK_BUF)
        len = BLOCK_BUF;

    char *buf = (char *)task->stack->r1;
    memcpy(buf, block->buf, len);

    /* still buzy until driver write response */
    return len;
}

int block_driver_write (struct block *block, struct task_control_block *task)
{
    struct block_response *response = (void *)task->stack->r1;
    char *buf = response->buf;
    int len = response->transfer_len;
    if (len > BLOCK_BUF)
        len = BLOCK_BUF;

    if (len > 0) {
        memcpy(buf, block->buf, len);
    }
    block->buzy = 0;
    return len;
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
int block_request_readable (struct block *block, struct task_control_block *task)
{
    if (block->request_pid == 0) {
        /* try to send request */
        struct file *driver = block->driver_file;

        int len = task->stack->r2;
        if (len > BLOCK_BUF)
            len = BLOCK_BUF;

        struct block_request request = {
            .cmd = BLOCK_CMD_READ,
            .task = task->pid,
            .fd = task->stack->r0,
            .size = len,
            .pos = block->pos
        };

        if (driver->writable(driver, (char *)&request, sizeof(request), task)) {
            driver->write(driver, (char *)&request, sizeof(request), task);

            block->request_pid = task->pid;
            block->buzy = 1;
        }
    }
    else if (block->request_pid == task->pid && !block->buzy) {
        return 1;
    }

    task->status = TASK_WAIT_READ;
    return 0;
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
int block_request_writable (struct block *block, struct task_control_block *task)
{
    if (block->request_pid == 0) {
        /* try to send request */
        struct file *driver = block->driver_file;

        int len = task->stack->r2;
        if (len > BLOCK_BUF)
            len = BLOCK_BUF;

        struct block_request request = {
            .cmd = BLOCK_CMD_WRITE,
            .task = task->pid,
            .fd = task->stack->r0,
            .size = len,
            .pos = block->pos
        };

        if (driver->writable(driver, (char *)&request, sizeof(request), task)) {
            driver->write(driver, (char *)&request, sizeof(request), task);

            char *buf = (char *)task->stack->r1;
            memcpy(block->buf, buf, len);

            block->request_pid = task->pid;
            block->buzy = 1;
        }
    }
    else if (block->request_pid == task->pid && !block->buzy) {
        return 1;
    }

    task->status = TASK_WAIT_WRITE;
    return 0;
}

int block_request_read (struct block *block, struct task_control_block *task)
{
    if (block->transfer_len > 0) {
        char *buf = (char *)task->stack->r1;
        memcpy(buf, block->buf, block->transfer_len);
    }

    block->request_pid = 0;
    return block->transfer_len;
}

int block_request_write (struct block *block, struct task_control_block *task)
{
    block->request_pid = 0;
    return block->transfer_len;
}

int block_init(int fd, int driver_pid, struct file *files[],
               struct memory_pool *memory_pool)
{
    struct block *block;

    block = memory_pool_alloc(memory_pool, sizeof(*block));

    if (!block)
        return -1;

    block->driver_pid = driver_pid;
    block->driver_file = files[driver_pid];
    block->request_pid = 0;
    block->buzy = 0;
	block->file.readable = block_readable;
	block->file.writable = block_writable;
	block->file.read = block_read;
	block->file.write = block_write;
    files[fd] = &block->file;
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

int block_readable (struct file *file, char *buf, size_t size,
                    struct task_control_block *task)
{
    struct block *block = container_of(file, struct block, file);
    if (block->driver_pid == task->pid) {
        return block_driver_readable(block, task);
    }
    else {
        return block_request_readable(block, task);
    }
}

int block_writable (struct file *file, char *buf, size_t size,
                    struct task_control_block *task)
{
    struct block *block = container_of(file, struct block, file);
    if (block->driver_pid == task->pid) {
        return block_driver_writable(block, task);
    }
    else {
        return block_request_writable(block, task);
    }
}

int block_read (struct file *file, char *buf, size_t size,
                struct task_control_block *task)
{
    struct block *block = container_of(file, struct block, file);
    if (block->driver_pid == task->pid) {
        return block_driver_read(block, task);
    }
    else {
        return block_request_read(block, task);
    }
}

int block_write (struct file *file, char *buf, size_t size,
                 struct task_control_block *task)
{
    struct block *block = container_of(file, struct block, file);
    if (block->driver_pid == task->pid) {
        return block_driver_write(block, task);
    }
    else {
        return block_request_write(block, task);
    }
}

