#ifndef LIST_H
#define LIST_H

#include "utils.h"

struct list {
    struct list *prev;
    struct list *next;
};

#define list_entry(list, type, member) \
    (container_of((list), type, member))

/* If no item is removed */
#define list_for_each(curr, list) \
    for ((curr) = (list)->next; (curr) != (list); (curr) = (curr)->next)

/* If some items may be removed */
#define list_for_each_safe(curr, next, list) \
    for ((curr) = (list)->next, (next) = (curr)->next; \
         (curr) != (list); (curr) = (next), (next) = (curr)->next)

void list_init(struct list *list);
int list_empty(struct list *list);
void list_remove(struct list *list);
void list_unshift(struct list *list, struct list *new);
void list_push(struct list *list, struct list* new);
struct list* list_shift(struct list* list);

#endif
