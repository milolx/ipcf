#include <stdio.h>
#include <pthread.h>
#include "compact.h"

#define MAX_BUF_SIZE	2000

static void *recv_tun(void *arg)
{
	char buf[MAX_BUF_SIZE];
	int n;
	pkt_t *pkt;
	struct list pkt_list;

	while (1) {
		n = tun_read(buf, MAX_BUF_SIZE);
		xmit_compress(buf, &pkt_list);
		LIST_FOR_EACH(pkt, node, &pkt_list)
			udp_send(pkt->data, pkt->len);
	}
}

static void *recv_udp(void *arg)
{
	char buf[MAX_BUF_SIZE];
	int n;
	pkt_t *ippkt, *send_back_pkt;

	while (1) {
		n = udp_recv(buf, MAX_BUF_SIZE);
		recv_compress(buf, n, &ippkt, &send_back_pkt);
		if (ippkt)
			tun_write(ippkt->data, ippkt->len);
		if (send_back_pkt)
			udp_send(send_back_pkt->data, send_back_pkt->len);
	}
}

int main()
{
	pthread_t rtun, rudp;

	compact_init();

	tun_init();
	udp_init();

	pthread_create(&rtun, NULL, recv_tun, NULL);
	pthread_create(&rudp, NULL, recv_udp, NULL);

	while (1)
		sleep(5);

	return 0;
}
