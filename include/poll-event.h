#ifndef __POLL_EVENT_H__
#define __POLL_EVENT_H__

#include <poll.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct poll_waiter;

struct poll_waiter *poll_fd_wait(int fd, short int events);
void poll_timer_wait(long long int msec);
void poll_timer_wait_until(long long int msec);
static inline void poll_immediate_wake(void) {poll_timer_wait(0);}

/* Wait until an event occurs. */
void poll_block(void);
void poll_cancel(struct poll_waiter *);

#ifdef  __cplusplus
}
#endif

#endif /* __POLL_EVENT_H__ */
