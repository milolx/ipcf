#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

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

/* Writes the 'size' bytes in 'buf' to 'stream' as hex bytes arranged 16 per
 * line.  Numeric offsets are also included, starting at 'ofs' for the first
 * byte in 'buf'.  If 'ascii' is true then the corresponding ASCII characters
 * are also rendered alongside. */
static inline void
hex_dump(FILE *stream, const void *buf_, size_t size, uintptr_t ofs, bool ascii)
{
	const uint8_t *buf = buf_;
	const size_t per_line = 16; /* Maximum bytes per line. */

	while (size > 0)
	{
		size_t start, end, n;
		size_t i;

		/* Number of bytes on this line. */
		start = ofs % per_line;
		end = per_line;
		if (end - start > size)
			end = start + size;
		n = end - start;

		/* Print line. */

		fprintf(stream, "%04x  ", (uintmax_t) ((int)(ofs/per_line)*per_line));
		for (i = 0; i < start; i++)
			fprintf(stream, "   ");
		for (; i < end; i++)
			fprintf(stream, "%02x%c",
					buf[i - start], i == per_line / 2 - 1? '-' : ' ');
		if (ascii)
		{
			for (; i < per_line; i++)
				fprintf(stream, "   ");
			fprintf(stream, "|");
			for (i = 0; i < start; i++)
				fprintf(stream, " ");
			for (; i < end; i++) {
				int c = buf[i - start];
				putc(c >= 32 && c < 127 ? c : '.', stream);
			}
			for (; i < per_line; i++)
				fprintf(stream, " ");
			fprintf(stream, "|");
		}
		fprintf(stream, "\n");

		ofs += n;
		buf += n;
		size -= n;
	}
}

#endif /* __UTIL_H__ */
