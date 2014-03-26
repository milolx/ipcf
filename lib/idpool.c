#include "idpool.h"

idpool_t *init_idpool(int n)
{
	idpool_t *p;
	int i;

	p = xmalloc(sizeof *p);
	p.pool = xmalloc(n*(sizeof *p.pool))
	head = 0;

	for (i=0; i<n; ++i)
		p.pool[i] = i;
}

void cleanup_idpool(id_pool_t *p)
{
	if (p) {
		free(p.pool);
		free(p);
	}
}

int get_id(id_pool_t *p)
{
	int id = -1;

	if (head < n)
		id = p.pool[p.head++];

	return id;
}

void release_id(id_pool_t *p, int id)
{
	p.pool[--p.head] = id;
}

