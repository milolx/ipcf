#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>

#define OBJECT_OFFSETOF(OBJECT, MEMBER) offsetof(typeof(*(OBJECT)), MEMBER)
#define OBJECT_CONTAINING(POINTER, OBJECT, MEMBER)                      \
    ((typeof(OBJECT)) ((char *) (POINTER) - OBJECT_OFFSETOF(OBJECT, MEMBER)))
#define ASSIGN_CONTAINER(OBJECT, POINTER, MEMBER) \
    ((OBJECT) = OBJECT_CONTAINING(POINTER, OBJECT, MEMBER), (void) 0)

/* Returns true if X is a power of 2, otherwise false. */
#define IS_POW2(X) ((X) && !((X) & ((X) - 1)))
static inline bool
is_pow2(uintmax_t x)
{
	return IS_POW2(x);
}   

static inline uint32_t get_unaligned_u32(const uint32_t *p_)
{
	const uint8_t *p = (const uint8_t *) p_;
	return ntohl((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static inline void *
xmalloc(size_t size)
{
	void *p = malloc(size ? size : 1);
	if (p == NULL) {
		printf("err:(malloc) out of memory\n");
		exit(1);
	}
	return p;
}

static inline void *
xcalloc(size_t count, size_t size)
{
	void *p = count && size ? calloc(count, size) : malloc(1);
	if (p == NULL) {
		printf("err:(calloc) out of memory\n");
		exit(1);
	}
	return p;
}

static inline void *
xrealloc(void *p, size_t size)
{   
	p = realloc(p, size ? size : 1);
	if (p == NULL) {
		printf("err:(realloc) out of memory\n");
		exit(1);
	}
	return p;
}

#endif /* __UTIL_H__ */
