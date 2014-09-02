#include "util.h"
#include "idpool.h"

idpool_t *init_idpool(int n)
{
	idpool_t *p;
	int i;

	p = xmalloc(sizeof *p);
	p->pool = xmalloc(n*(sizeof *p->pool));
	p->n = n;
	p->head = 0;
	p->tail = n-1;

	for (i=0; i<n; ++i)
		p->pool[i] = i;

	return p;
}

void cleanup_idpool(idpool_t *p)
{
	if (p) {
		free(p->pool);
		free(p);
	}
}

int get_id(idpool_t *p)
{
	int id = -1;
	int tail_next = (p->tail + 1) % p->n;

	if (p->head != tail_next) {
		id = p->pool[p->head];
		p->head = (p->head + 1) % p->n;
	}

	return id;
}

void release_id(idpool_t *p, int id)
{
	int tail_next = (p->tail + 1) % p->n;

	if (tail_next != p->head) {
		p->tail = tail_next;
		p->pool[p->tail] = id;
	}
}

