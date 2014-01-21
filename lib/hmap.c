#include "hmap.h"

void
hmap_init(struct hmap *hmap)
{
    hmap->buckets = &hmap->one;
    hmap->one = NULL;
    hmap->mask = 0;
    hmap->n = 0;
}

void
hmap_destroy(struct hmap *hmap)
{
    if (hmap && hmap->buckets != &hmap->one) {
        free(hmap->buckets);
    }
}

void
hmap_swap(struct hmap *a, struct hmap *b)
{
    struct hmap tmp = *a;
    *a = *b;
    *b = tmp;
    hmap_moved(a);
    hmap_moved(b);
}

void
hmap_moved(struct hmap *hmap)
{
    if (!hmap->mask) {
        hmap->buckets = &hmap->one;
    }
}

static void
resize(struct hmap *hmap, size_t new_mask)
{
	struct hmap tmp;
	size_t i;

#ifdef __DEBUG__
	if (!is_pow2(new_mask + 1)) {
		printf("hmap err: mask(0x%08x+1) is not pow2\n", new_mask);
		exit(1);
	}
#endif

	hmap_init(&tmp);
	if (new_mask) {
		tmp.buckets = xmalloc(sizeof *tmp.buckets * (new_mask + 1));
		tmp.mask = new_mask;
		for (i = 0; i <= tmp.mask; i++) {
			tmp.buckets[i] = NULL;
		}
	}
	for (i = 0; i <= hmap->mask; i++) {
		struct hmap_node *node, *next;
#ifdef __DEBUG__
		int count = 0;
#endif
		for (node = hmap->buckets[i]; node; node = next) {
			next = node->next;
			hmap_insert_fast(&tmp, node, node->hash);
#ifdef __DEBUG__
			count++;
#endif
		}
#ifdef __DEBUG__
		if (count > 5) {
			printf("hmap warning:%d nodes in bucket (%d nodes, %d buckets)\n",
					count, hmap->n, hmap->mask + 1);
		}
#endif
	}
	hmap_swap(hmap, &tmp);
	hmap_destroy(&tmp);
}

static size_t
calc_mask(size_t capacity)
{
	size_t mask = capacity / 2;
	mask |= mask >> 1;
	mask |= mask >> 2;
	mask |= mask >> 4;
	mask |= mask >> 8;
	mask |= mask >> 16;
#if SIZE_MAX > UINT32_MAX
	mask |= mask >> 32;
#endif

	mask |= (mask & 1) << 1;

	return mask;
}

void
hmap_expand(struct hmap *hmap)
{
	size_t new_mask = calc_mask(hmap->n);
	if (new_mask > hmap->mask) {
		resize(hmap, new_mask);
	}
}

