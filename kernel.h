#ifndef KERNEL_H
#define KERNEL_H

#include "task.h"
#include "file.h"
#include "event-monitor.h"
#include <stddef.h>

int _read(struct file *file, struct file_request *request,
          struct event_monitor *monitor);
int _write(struct file *file, struct file_request *request,
           struct event_monitor *monitor);

#endif
