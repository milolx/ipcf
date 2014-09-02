#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "compact.h"
#include "split.h"
#include "rand.h"

/*
void proc_timer(void);
int upper_send(u8 dmac, u8 smac, void *msg, int msglen);
int upper_recv(u8 *buf, int *len);
int lower_fetch(u8 *buf, int *len);
int lower_put(void *raw_frm, int rawlen);
void split_init();
void split_cleanup();
*/

/*
 * ip stack -> tun ->
 *	xmit_compress ->
 *		split (upper_send, lower_fetch) ->
 *			udp send -> ... -> (remote) udp recv ->
 *		unsplit (lower_put, upper_recv) ->
 *	recv_compress ->
 * tun -> ip stack
 */

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

static u32 tot = 1;
static u32 full_ip = 1;
static u32 compact_ip = 0;
static u32 payload = 0;

static void *recv_tun(void *arg)
{
	char buf[MAX_BUF_SIZE];
	int n, n2;
	pkt_t *pkt;
	struct list pkt_list;

	while (1) {
		n = tun_read(buf, MAX_BUF_SIZE);
		if (n <= 0) {
			printf("turn_read err, n=%d\n", n);
			continue;
		}

		full_ip += n;
		payload += n - 28;	// iphdr(20), udphdr(8)
		//<<<<<<<<<< IP

		xmit_compress(buf, &pkt_list);
		//>>>>>>>>>> compacted IP
		LIST_FOR_EACH(pkt, node, &pkt_list) {
			n2 = upper_send(2, 1, pkt->data, pkt->len);
			compact_ip += pkt->len;
			if (n2 != n)
				printf("upper_send error, ret=%d\n", n2);
		}
	}

	return NULL;
}

static void *recv_split(void *arg)
{
	u8 buf[MAX_BUF_SIZE];
	int n, n2;

	while (1) {
		n = MAX_BUF_SIZE;
		n2 = lower_fetch(buf, &n);
		if (n2 <= 0) {
			usleep(500);
			continue;
		}

		udp_send((char *)buf, n2);
		tot += n2;
	}

	return NULL;
}

static void *recv_udp(void *arg)
{
	char buf[MAX_BUF_SIZE];
	int n;

	while (1) {
		n = udp_recv(buf, MAX_BUF_SIZE);
		if (n <= 0) {
			printf("udp_recv, n=%d\n", n);
			continue;
		}
		//<<<<<<<<<< splited & packed data frame

		lower_put(buf, n);
	}

	return NULL;
}

static void *recv_unsplit(void *arg)
{
	u8 buf[MAX_BUF_SIZE];
	int n, n2;
	pkt_t *ippkt, *send_back_pkt;

	while (1) {
		n = MAX_BUF_SIZE;
		n2 = upper_recv(buf, &n);
		if (n2 <= 0) {
			usleep(500);
			continue;
		}
		//<<<<<<<<<< compacted ip

		recv_compress(buf, n, &ippkt, &send_back_pkt);
		if (ippkt)
			tun_write((char *)ippkt->data, ippkt->len);
		if (send_back_pkt)
			udp_send((char *)send_back_pkt->data,
					send_back_pkt->len);
	}

	return NULL;
}

int main()
{
	pthread_t rtun, rsplit, rudp, runsplit;

	compact_init();
	split_init();

	tun_init();
	udp_init();

	pthread_create(&rtun, NULL, recv_tun, NULL);
	pthread_create(&rsplit, NULL, recv_split, NULL);
	pthread_create(&rudp, NULL, recv_udp, NULL);
	pthread_create(&runsplit, NULL, recv_unsplit, NULL);

	while (1) {
		sleep(1);

		proc_timer();

		printf("effective payload = %d\n", payload);
		printf("compact ip tot = %d\n", compact_ip);
		printf("full ip tot = %d\n", full_ip);
		printf("everything(including re-transmition), tot = %d\n", tot);
		printf("data ratio(payload/tot) = %f\n",(float)(payload/tot));
		printf("compact ratio(cip/fip) = %f\n",
				(float)(compact_ip/full_ip));
		printf("split ratio(cip/tot) = %f\n",
				(float)(compact_ip/tot));
		printf("-----\n\n");
	}

	return 0;
}
