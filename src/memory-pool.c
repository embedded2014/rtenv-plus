#include "memory-pool.h"



void memory_pool_init(struct memory_pool *pool, size_t size, char *memory)
{
    pool->offset = 0;
    pool->size = size;
    pool->memory = memory;
}

void *memory_pool_alloc(struct memory_pool *pool, size_t size)
{
    if (pool->offset + size <= pool->size) {
        char *alloc = pool->memory + pool->offset;
        pool->offset += size;
        return alloc;
    }

    return NULL;
}
