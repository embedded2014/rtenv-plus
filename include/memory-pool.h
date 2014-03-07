#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <stddef.h>

struct memory_pool {
    int offset;
    size_t size;
    char *memory;
};

void memory_pool_init(struct memory_pool *pool, size_t size, char *memory);
void *memory_pool_alloc(struct memory_pool *pool, size_t size);

#endif
