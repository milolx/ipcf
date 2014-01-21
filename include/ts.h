#ifndef __TS_H__
#define __TS_H__

#include <time.h>

static inline long long int ts_msec(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		printf("clock_gettime err\n");
		exit(1);
	}

	return (long long int) ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);
}

#endif /* __TS_H__ */
