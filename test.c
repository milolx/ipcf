#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "compact.h"

extern int tun_init();
extern int tun_read(char *buf, int n);
extern int tun_write(char *buf, int n);
extern int udp_send(char *buf, int n);
extern int udp_recv(char *buf, int n);
extern int udp_init();

extern void xmit_compress(void *ippkt, struct list *pkt_list);
extern int recv_compress(void *cpkt, int len, pkt_t **ippkt,
		pkt_t **send_back_pkt);
extern void compact_init();

#define MAX_BUF_SIZE	2000

static void *recv_tun(void *arg)
{
	char buf[MAX_BUF_SIZE];
	int n;
	pkt_t *pkt, *nxt;
	struct list pkt_list;

	while (1) {
		n = tun_read(buf, MAX_BUF_SIZE);
		if (n <= 0) {
			printf("turn_read err, n=%d\n", n);
			continue;
		}
		xmit_compress(buf, &pkt_list);
		LIST_FOR_EACH_SAFE(pkt, nxt, node, &pkt_list) {
			udp_send((char *)pkt->data, pkt->len);
			list_remove(&pkt->node);
			free(pkt);
		}
	}

	return NULL;
}

static void *recv_udp(void *arg)
{
	char buf[MAX_BUF_SIZE];
	int n;
	pkt_t *ippkt, *send_back_pkt;

	while (1) {
		n = udp_recv(buf, MAX_BUF_SIZE);
		if (n <= 0) {
			printf("udp_recv, n=%d\n", n);
			continue;
		}
		recv_compress(buf, n, &ippkt, &send_back_pkt);
		if (ippkt) {
			tun_write((char *)ippkt->data, ippkt->len);
			free(ippkt);
		}
		if (send_back_pkt) {
			udp_send((char *)send_back_pkt->data, send_back_pkt->len);
			free(send_back_pkt);
		}
	}

	return NULL;
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
