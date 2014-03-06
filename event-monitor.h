#ifndef EVENT_H
#define EVENT_H

#include "kconfig.h"
#include "task.h"

struct event_monitor;

typedef int (*event_monitor_handler)(struct event_monitor *monitor,
                                     int event,
                                     struct task_control_block *task,
                                     void *data);

struct event {
    int registerd;
    int pending;
    event_monitor_handler handler;
    void *data;
    struct list list;
};

struct event_monitor {
    struct event *events;
    struct list *ready_list;
};

void event_monitor_init(struct event_monitor *monitor,
                        struct event *events,
                        struct list *ready_list);
int event_monitor_find_free(struct event_monitor *monitor);
void event_monitor_register(struct event_monitor *monitor, int event,
                            event_monitor_handler handler, void *data);
void event_monitor_block(struct event_monitor *monitor, int event,
                         struct task_control_block *task);
void event_monitor_release(struct event_monitor *monitor, int event);
void event_monitor_serve(struct event_monitor *monitor);

#endif
