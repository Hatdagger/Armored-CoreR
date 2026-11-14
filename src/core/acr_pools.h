/* The Xbox-class memory budget (docs/DESIGN.md): one 64 MB block split into
 * three pools. Allocation is linear; a pool is freed all at once with
 * acr_pool_reset (for example when a match or stage ends). */
#ifndef ACR_POOLS_H
#define ACR_POOLS_H

#include <stddef.h>
#include <stdint.h>

typedef enum acr_pool_id {
    ACR_POOL_GAME = 0,    /* 32 MB: parts, ACs, stages, missions, simulation */
    ACR_POOL_TEXTURE,     /* 24 MB: graphics */
    ACR_POOL_AUDIO,       /*  8 MB: sound */
    ACR_POOL_COUNT
} acr_pool_id;

#define ACR_MB(n) ((size_t)(n) * 1024u * 1024u)
#define ACR_POOL_GAME_BYTES    ACR_MB(32)
#define ACR_POOL_TEXTURE_BYTES ACR_MB(24)
#define ACR_POOL_AUDIO_BYTES   ACR_MB(8)
#define ACR_POOLS_TOTAL_BYTES  (ACR_POOL_GAME_BYTES + ACR_POOL_TEXTURE_BYTES + ACR_POOL_AUDIO_BYTES)

typedef struct acr_pool {
    uint8_t *base;
    size_t   capacity;
    size_t   used;
    size_t   high_water;   /* most ever used: shows how close content gets */
} acr_pool;

typedef struct acr_pools {
    acr_pool pool[ACR_POOL_COUNT];
} acr_pools;

/* `block` must hold ACR_POOLS_TOTAL_BYTES. The platform layer provides it
 * (malloc on PC, a fixed region on the Xbox). Returns 0 if it is too small. */
int acr_pools_init(acr_pools *p, void *block, size_t block_bytes);

/* NULL when the request would pass the pool's budget. `align` is a power of
 * two up to 4096 (0 means 16). */
void *acr_pool_alloc(acr_pools *p, acr_pool_id id, size_t bytes, size_t align);

void acr_pool_reset(acr_pools *p, acr_pool_id id);

#endif
