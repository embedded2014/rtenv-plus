#include "task.h"

#include <stddef.h>

unsigned int *init_task(unsigned int *stack, void (*start)())
{
	stack += STACK_SIZE - 9; /* End of stack, minus what we're about to push */
	stack[8] = (unsigned int)start;
	return stack;
}

int
task_push (struct task_control_block **list, struct task_control_block *item)
{
	if (list && item) {
		/* Remove itself from original list */
		if (item->prev)
			*(item->prev) = item->next;
		if (item->next)
			item->next->prev = item->prev;
		/* Insert into new list */
		while (*list) list = &((*list)->next);
		*list = item;
		item->prev = list;
		item->next = NULL;
		return 0;
	}
	return -1;
}

struct task_control_block*
task_pop (struct task_control_block **list)
{
	if (list) {
		struct task_control_block *item = *list;
		if (item) {
			*list = item->next;
			if (item->next)
				item->next->prev = list;
			item->prev = NULL;
			item->next = NULL;
			return item;
		}
	}
	return NULL;
}
