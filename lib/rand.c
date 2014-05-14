#include <stdint.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "rand.h"
#include "hash.h"

static const char urandom[] = "/dev/urandom";
static uint32_t seed = 0;

int
get_entropy(void *buffer, size_t n)
{
	int err;
	int fd;

	fd = open(urandom, O_RDONLY);
	if (fd < 0) {
		perror("%s\n", urandom);
		return errno;
	}

	err = read(fd, buffer, n);
	close(fd);

	return err;
}

static uint32_t random_next(void);

void
random_init(void)
{
    while (!seed) {
        struct timeval tv;
        uint32_t entropy;
        pthread_t self;

        gettimeofday(&tv, NULL);
        get_entropy(&entropy, 4);
        self = pthread_self();

        seed = (tv.tv_sec ^ tv.tv_usec ^ entropy
                  ^ hash_bytes(&self, sizeof self));
    }
}

void
random_set_seed(uint32_t seed_)
{
    seed = seed_;
}

void
random_bytes(void *p_, size_t n)
{
    uint8_t *p = p_;

    random_init();

    for (; n > 4; p += 4, n -= 4) {
        uint32_t x = random_next();
        memcpy(p, &x, 4);
    }

    if (n) {
        uint32_t x = random_next();
        memcpy(p, &x, n);
    }
}


uint32_t
random_uint32(void)
{
    random_init();
    return random_next();
}

uint64_t
random_uint64(void)
{
    uint64_t x;

    random_init();

    x = random_next();
    x |= (uint64_t) random_next() << 32;
    return x;
}

static uint32_t
random_next(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;

    return seed;
}
