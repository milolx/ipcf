#ifndef __ID_POOL_H__
#define __ID_POOL_H__

typedef struct {
	int n;
	int *pool;
	int head;
}idpool_t;

idpool_t *init_idpool(int n);
void cleanup_idpool(id_pool_t *p);
int get_id(id_pool_t *p);
void release_id(id_pool_t *p, int id);

#endif /* __ID_POOL_H__ */
