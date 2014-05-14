#ifndef __RAND_H__
#define __RAND_H__

#include <stddef.h>
#include <stdint.h>

void random_init(void);
void random_set_seed(uint32_t);

void random_bytes(void *, size_t);
uint32_t random_uint32(void);
uint64_t random_uint64(void);

static inline int
random_range(int max)
{
    return random_uint32() % max;
}

static inline uint8_t
random_uint8(void)
{
    return random_uint32();
}

static inline uint16_t
random_uint16(void)
{
    return random_uint32();
}

#endif /* __RAND_H__ */
