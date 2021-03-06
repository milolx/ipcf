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
extern void timer_event();

#define MAX_BUF_SIZE	2000

static u32 tot_s = 1;		// everything to comm channel
static u32 tot_r = 1;		// everyting from comm channel
static u32 full_ip_s = 1;	// full ip recv from tun dev to be sent
static u32 compact_ip_s = 0;	// compact pkt + ctrl pkt for send data
static u32 full_ip_r = 1;	// full ip submit to tun dev
static u32 compact_ip_r = 0;	// compact pkt recv from comm channel and
				// generated ctrl pkt for recv(ack)

static void *recv_tun(void *arg)
{
	char buf[MAX_BUF_SIZE];
	int n, n2;
	pkt_t *pkt, *nxt;
	struct list pkt_list;

	while (1) {
		n = tun_read(buf, MAX_BUF_SIZE);
		if (n <= 0) {
			printf("turn_read err, n=%d\n", n);
			continue;
		}
		full_ip_s += n;

		xmit_compress(buf, &pkt_list);
		//>>>>>>>>>> compacted IP
		LIST_FOR_EACH_SAFE(pkt, nxt, node, &pkt_list) {
			compact_ip_s += pkt->len;

			n2 = upper_send(2, 1, pkt->data, pkt->len);
			if (n2 != pkt->len)
				printf("upper_send error, ret=%d, len=%d\n", n2, pkt->len);

			list_remove(&pkt->node);
			free(pkt);
		}
	}

	return NULL;
}

static void *recv_split(void *arg)
{
	u8 buf[MAX_BUF_SIZE];
	int n;

	while (1) {
		n = MAX_BUF_SIZE;
		lower_fetch(buf, &n);
		if (n <= 0) {
			usleep(500);
			continue;
		}

		udp_send((char *)buf, n);
		tot_s += n;
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
		tot_r += n;
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
		if (n2 > 0)
			printf("upper_recv: drop %d byte(s)\n", n2);
		if (!n) {
			usleep(500);
			continue;
		}
		compact_ip_r += n;
		//<<<<<<<<<< compacted ip

		recv_compress(buf, n, &ippkt, &send_back_pkt);
		if (ippkt) {
			full_ip_r += ippkt->len;
			tun_write((char *)ippkt->data, ippkt->len);
			free(ippkt);
		}
		if (send_back_pkt) {
			compact_ip_r += send_back_pkt->len;
			n2 = upper_send(2, 1, send_back_pkt->data, send_back_pkt->len);
			if (n2 != send_back_pkt->len)
				printf("upper_sendback error, ret=%d\n", n2);

			free(send_back_pkt);
		}
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
		timer_event();
		proc_timer();

		printf("compact ip sent = %d\n", compact_ip_s);
		printf("compact ip recv = %d\n", compact_ip_r);
		printf("full ip recv = %d\n", full_ip_r);
		printf("full ip submit = %d\n", full_ip_s);
		printf("everything send to channel = %d\n", tot_s);
		printf("everything recv from channel = %d\n", tot_r);
		printf("compact ratio(cip/fip) = (%d/%d) = %f\n",
				compact_ip_s+compact_ip_r, full_ip_s+full_ip_r,
				(float) (compact_ip_s+compact_ip_r)/(full_ip_s+full_ip_r));
		printf("tot ratio(tot/full ip) = (%d/%d)= %f\n",
				tot_s+tot_r, full_ip_s+full_ip_r,
				(float) (tot_s+tot_r) / (full_ip_s+full_ip_r));
		printf("-----\n\n");
	}

	return 0;
}
