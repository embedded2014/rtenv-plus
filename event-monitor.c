#include "event-monitor.h"



void event_monitor_init(struct event_monitor *monitor,
                        struct event *events,
                        struct task_control_block **ready_list)
{
    int i;

    monitor->events = events;
    monitor->ready_list = ready_list;

    for (i = 0; i < EVENT_LIMIT; i++) {
        events[i].pending = 0;
        events[i].handler = 0;
        events[i].data = 0;
        events[i].list = 0;
    }
}

void event_monitor_register(struct event_monitor *monitor, int event,
                            event_monitor_handler handler, void *data)
{
    monitor->events[event].handler = handler;
    monitor->events[event].data = data;
}

void event_monitor_block(struct event_monitor *monitor, int event,
                         struct task_control_block *task)
{
    task_push(&monitor->events[event].list, task);
}

void event_monitor_release(struct event_monitor *monitor, int event)
{
    monitor->events[event].pending = 1;
}

void event_monitor_serve(struct event_monitor *monitor)
{
    int i;
    for (i = 0; i < EVENT_LIMIT; i++) {
        if (monitor->events[i].pending) {
            struct event *event = &monitor->events[i];
            struct task_control_block *task;

            for (task = event->list;
                    task && event->handler(monitor, i, task, event->data);
                    task = event->list) {

                task_pop(&event->list);
                task_push(&monitor->ready_list[task->priority], task);
                task->status = TASK_READY;
            }

            event->pending = 0;

            /* If someone pending events, rescan events */
            i = 0;
        }
    }
}
