#include <stdio.h>
#include <pthread.h>

static void *recv_tun(void *arg)
{
	char buf[2000];
	int n;

	while (1) {
		n = tun_read(buf, 2000);
		udp_send(buf, n);
	}
}

static void *recv_udp(void *arg)
{
	char buf[2000];
	int n;

	while (1) {
		n = udp_recv(buf, 2000);
		tun_write(buf, n);
	}
}

int main()
{
	pthread_t rtun, rudp;

	tun_init();
	udp_init();

	pthread_create(&rtun, NULL, recv_tun, NULL);
	pthread_create(&rudp, NULL, recv_udp, NULL);

	while (1)
		sleep(5);

	return 0;
}
