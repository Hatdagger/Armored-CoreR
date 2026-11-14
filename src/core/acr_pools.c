#include "acr_pools.h"

int acr_pools_init(acr_pools *p, void *block, size_t block_bytes) {
    static const size_t sizes[ACR_POOL_COUNT] = {
        ACR_POOL_GAME_BYTES, ACR_POOL_TEXTURE_BYTES, ACR_POOL_AUDIO_BYTES };
    if (!p || !block || block_bytes < ACR_POOLS_TOTAL_BYTES) return 0;
    uint8_t *at = (uint8_t *)block;
    for (int i = 0; i < ACR_POOL_COUNT; ++i) {
        p->pool[i].base = at;
        p->pool[i].capacity = sizes[i];
        p->pool[i].used = 0;
        p->pool[i].high_water = 0;
        at += sizes[i];
    }
    return 1;
}

void *acr_pool_alloc(acr_pools *p, acr_pool_id id, size_t bytes, size_t align) {
    if (!p || (unsigned)id >= ACR_POOL_COUNT || bytes == 0) return NULL;
    if (align == 0) align = 16;
    if ((align & (align - 1)) != 0 || align > 4096) return NULL;
    acr_pool *pool = &p->pool[id];
    const uintptr_t base = (uintptr_t)pool->base;
    const uintptr_t start = (base + pool->used + align - 1) & ~(uintptr_t)(align - 1);
    const size_t offset = (size_t)(start - base);
    if (offset > pool->capacity || bytes > pool->capacity - offset) return NULL;
    pool->used = offset + bytes;
    if (pool->used > pool->high_water) pool->high_water = pool->used;
    return (void *)start;
}

void acr_pool_reset(acr_pools *p, acr_pool_id id) {
    if (p && (unsigned)id < ACR_POOL_COUNT) p->pool[id].used = 0;
}
