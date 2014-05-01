#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#include <semaphore.h>

#if __GNUC__ < 4 || __GNUC_MINOR__ < 7
#error "gun c compiler version < 4.7"
#endif

#define ATOMIC_VAR_INIT(VALUE) (VALUE)
#define atomic_init(OBJECT, VALUE) (*(OBJECT) = (VALUE), (void) 0)

typedef unsigned int atomic_cnt_t;

typedef struct {
	sem_t sem;
	atomic_cnt_t cnt;
}light_lock_t;

static inline void _lock_init(light_lock_t *lock)
{
	sem_init(&lock->sem, 0, 0);
	__atomic_store_n(&lock->cnt, 0, __ATOMIC_SEQ_CST);
}

static inline void _lock(light_lock_t *lock)
{
	if (__atomic_fetch_add(&lock->cnt, 1, __ATOMIC_ACQUIRE) > 0)
		sem_wait(&lock->sem);
}

static inline void _release(light_lock_t *lock)
{
	if (__atomic_sub_fetch(&lock->cnt, 1, __ATOMIC_RELEASE) > 0)
		sem_post(&lock->sem);
}

#endif /* __ATOMIC_H__ */
