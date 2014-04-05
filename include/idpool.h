#ifndef __ID_POOL_H__
#define __ID_POOL_H__

typedef struct {
	int n;
	int *pool;
	int head;
	int tail;
}idpool_t;

idpool_t *init_idpool(int n);
void cleanup_idpool(idpool_t *p);
int get_id(idpool_t *p);
void release_id(idpool_t *p, int id);

#endif /* __ID_POOL_H__ */
