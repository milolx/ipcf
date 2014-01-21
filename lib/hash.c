#include <string.h>
#include "hash.h"
#include "util.h"

/* Returns the hash of the 'n' bytes at 'p'. */
uint32_t
hash_bytes(const void *p_, size_t n)
{
    const uint32_t *p = p_;
    size_t orig_n = n;
    uint32_t hash;

    hash = 0;
    while (n >= 4) {
        hash = mhash_add(hash, get_unaligned_u32(p));
        n -= 4;
        p += 1;
    }

    if (n) {
        uint32_t tmp = 0;

        memcpy(&tmp, p, n);
        hash = mhash_add__(hash, tmp);
    }

    return mhash_finish(hash, orig_n);
}

