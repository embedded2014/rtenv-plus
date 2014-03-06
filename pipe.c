#include "pipe.h"



int pipe_read_release(struct event_monitor *monitor, int event,
                      struct task_control_block *task, void *data)
{
    struct file *file = data;
    struct file_request *request = (void*)task->stack->r0;

    return file_read(file, request, monitor);
}

int pipe_write_release(struct event_monitor *monitor, int event,
                       struct task_control_block *task, void *data)
{
    struct file *file = data;
    struct file_request *request = (void*)task->stack->r0;

    return file_write(file, request, monitor);
}
