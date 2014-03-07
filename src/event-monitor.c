#include "event-monitor.h"

#include "list.h"


void event_monitor_init(struct event_monitor *monitor,
                        struct event *events,
                        struct list *ready_list)
{
    int i;

    monitor->events = events;
    monitor->ready_list = ready_list;

    for (i = 0; i < EVENT_LIMIT; i++) {
        events[i].registerd = 0;
        events[i].pending = 0;
        events[i].handler = 0;
        events[i].data = 0;
        list_init(&events[i].list);
    }
}

int event_monitor_find_free(struct event_monitor *monitor)
{
    int i;
    for (i = 0; i < EVENT_LIMIT && monitor->events[i].registerd; i++);

    if (i == EVENT_LIMIT)
        return -1;

    return i;
}

void event_monitor_register(struct event_monitor *monitor, int event,
                            event_monitor_handler handler, void *data)
{
    monitor->events[event].registerd = 1;
    monitor->events[event].handler = handler;
    monitor->events[event].data = data;
}

void event_monitor_block(struct event_monitor *monitor, int event,
                         struct task_control_block *task)
{
    if (task->status == TASK_READY)
        list_push(&monitor->events[event].list, &task->list);
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
            struct list *curr, *next;

            list_for_each_safe (curr, next, &event->list) {
                task = list_entry(curr, struct task_control_block, list);
                if (event->handler
                        && event->handler(monitor, i, task, event->data)) {
                    list_push(&monitor->ready_list[task->priority], &task->list);
                    task->status = TASK_READY;
                }
            }

            event->pending = 0;

            /* If someone pending events, rescan events */
            i = 0;
        }
    }
}
