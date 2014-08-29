#include <stdio.h>

#include "hash.h"
#include "hmap.h"
#include "list.h"
#include "poll-event.h"
#include "ts.h"
#include "util.h"

typedef struct {
	struct hmap_node node;
	long long int ts;
}ts_node_t;

int main()
{
	uint32_t hash;
	struct hmap h;
	ts_node_t *p,*q;
	int cnt;
	int i;
	unsigned int min,max;
	unsigned int lmin,lmax;
	unsigned int n,b;
	unsigned int cn,cb;
	long long int last_print=0;

	hmap_init(&h);
	min = -1;
	max = 0;
	cn = cb = n = b = 0;
	while(1) {
		p = xmalloc(sizeof *p);
		p->ts = ts_msec();
		hash = hash_bytes(&p->ts, sizeof p->ts);
		hmap_insert(&h, &p->node, hash);

#if 0
		printf("ts=%lld hash=%08x\n", p->ts, hash);
		printf("\t%d nodes, %d buckets\n", h.n, h.mask+1);
		cnt = 0;
		HMAP_FOR_EACH_WITH_HASH(q, node, hash, &h) {
			if (q->ts == p->ts)
				printf("\tfound, ts=%lld\n", q->ts);
			++ cnt;
		}
		printf("\t%d same nodes\n", cnt);
#endif
		cn = h.n;
		cb = h.mask+1;

		lmin = -1;
		lmax = 0;
		for (i = 0; i <= h.mask; i++) {
			struct hmap_node *node, *next;
			int count = 0;
			for (node = h.buckets[i]; node; node = next) {
				next = node->next;
				count++;
			}
			if (count < lmin)
				lmin = count;
			if (count > lmax)
				lmax = count;
		}
		if (lmax > max) {
			max = lmax;
			min = lmin;
			n = h.n;
			b = h.mask+1;
			printf("%d nodes, %d buckets\n", n, b);
			printf("\tmin %d, max %d\n", min, max);
		}
		if (p->ts - last_print > 300*1000) {
			last_print = p->ts;
			printf("min %d, max %d\n", min, max);
			printf("\tmax->%d nodes, %d buckets\n", n, b);
			printf("\tcur->%d nodes, %d buckets, max=%d\n", cn, cb, lmax);
		}

		poll_timer_wait(1);

		poll_block();
	}

	return 0;
}
