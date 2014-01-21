#include "poll-loop.h"
#include "list.h"
#include "ts.h"

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include "coverage.h"
#include "dynamic-string.h"
#include "fatal-signal.h"
#include "socket-util.h"
#include "vlog.h"

/* An event that will wake the following call to poll_block(). */
struct poll_waiter {
    /* Set when the waiter is created. */
    struct list node;           /* Element in global waiters list. */
    int fd;                     /* File descriptor. */
    short int events;           /* Events to wait for (POLLIN, POLLOUT). */

    /* Set only when poll_block() is called. */
    struct pollfd *pollfd;      /* Pointer to element of the pollfds array. */
};

/* All active poll waiters. */
static struct list waiters = LIST_INITIALIZER(&waiters);

/* Time at which to wake up the next call to poll_block(), in milliseconds as
 * returned by time_msec(), LLONG_MIN to wake up immediately, or LLONG_MAX to
 * wait forever. */
static long long int timeout_when = LLONG_MAX;

static struct poll_waiter *new_waiter(int fd, short int events);

struct poll_waiter *
poll_fd_wait(int fd, short int events)
{
    return new_waiter(fd, events);
}

void
poll_timer_wait(long long int msec)
{
    long long int now = ts_msec();
    long long int when;

    if (msec <= 0) {
        /* Wake up immediately. */
        when = LLONG_MIN;
    } else if ((unsigned long long int) now + msec <= LLONG_MAX) {
        /* Normal case. */
        when = now + msec;
    } else {
        /* now + msec would overflow. */
        when = LLONG_MAX;
    }

    poll_timer_wait_until(when);
}

void
poll_timer_wait_until(long long int when)
{
    if (when < timeout_when) {
        timeout_when = when;
    }
}

static int
time_poll(struct pollfd *pollfds, int n_pollfds, long long int timeout_when,
          int *elapsed)
{
	long long int start;
	int retval;

	start = ts_msec();

	for (;;) {
		long long int now = ts_msec();
		int time_left;

		if (now >= timeout_when) {
			time_left = 0;
		} else if ((unsigned long long int) timeout_when - now > INT_MAX) {
			time_left = INT_MAX;
		} else {
			time_left = timeout_when - now;
		}

		retval = poll(pollfds, n_pollfds, time_left);
		if (retval < 0) {
			retval = -errno;
		}

		if (retval != -EINTR) {
			break;
		}
	}
	*elapsed = ts_msec() - start;
	return retval;
}


/* Blocks until one or more of the events registered with poll_fd_wait()
 * occurs, or until the minimum duration registered with poll_timer_wait()
 * elapses, or not at all if poll_immediate_wake() has been called. */
void
poll_block(void)
{
    static struct pollfd *pollfds;
    static size_t max_pollfds;

    struct poll_waiter *pw, *next;
    int n_waiters, n_pollfds;
    int elapsed;
    int retval;

    n_waiters = list_size(&waiters);
    if (max_pollfds < n_waiters) {
        max_pollfds = n_waiters;
        pollfds = xrealloc(pollfds, max_pollfds * sizeof *pollfds);
    }

    n_pollfds = 0;
    LIST_FOR_EACH (pw, node, &waiters) {
        pw->pollfd = &pollfds[n_pollfds];
        pollfds[n_pollfds].fd = pw->fd;
        pollfds[n_pollfds].events = pw->events;
        pollfds[n_pollfds].revents = 0;
        n_pollfds++;
    }

    retval = time_poll(pollfds, n_pollfds, timeout_when, &elapsed);
    if (retval < 0) {
        printf("time_poll err\n");
    }
#ifdef __DEBUG__
#ifdef __DEBUG_POLL__
    else if (!retval) {
        printf("poll elapsed: %d msec\n", elapsed);
    }
#endif
#endif

    LIST_FOR_EACH_SAFE (pw, next, node, &waiters) {
        poll_cancel(pw);
    }

    timeout_when = LLONG_MAX;
}

/* Cancels the file descriptor event registered with poll_fd_wait() using 'pw',
 * the struct poll_waiter returned by that function.
 *
 * An event registered with poll_fd_wait() may be canceled from its time of
 * registration until the next call to poll_block().  At that point, the event
 * is automatically canceled by the system and its poll_waiter is freed. */
void
poll_cancel(struct poll_waiter *pw)
{
    if (pw) {
        list_remove(&pw->node);
        free(pw);
    }
}

/* Creates and returns a new poll_waiter for 'fd' and 'events'. */
static struct poll_waiter *
new_waiter(int fd, short int events)
{
    struct poll_waiter *waiter = xzalloc(sizeof *waiter);
    waiter->fd = fd;
    waiter->events = events;
    list_push_back(&waiters, &waiter->node);
    return waiter;
}

