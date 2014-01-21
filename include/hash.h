#ifndef __HASH_H__
#define __HASH_H__

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Murmurhash by Austin Appleby,
 */

static inline uint32_t
hash_rot(uint32_t x, int k)
{
    return (x << k) | (x >> (32 - k));
}

static inline uint32_t mhash_add__(uint32_t hash, uint32_t data)
{
    data *= 0xcc9e2d51;
    data = hash_rot(data, 15);
    data *= 0x1b873593;
    return hash ^ data;
}

static inline uint32_t mhash_add(uint32_t hash, uint32_t data)
{
    hash = mhash_add__(hash, data);
    hash = hash_rot(hash, 13);
    return hash * 5 + 0xe6546b64;
}

static inline uint32_t mhash_finish(uint32_t hash, size_t n_bytes)
{
    hash ^= n_bytes;
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;
    return hash;
}

uint32_t hash_bytes(const void *, size_t n_bytes);

#ifdef __cplusplus
}
#endif

#endif /* __HASH_H__ */
