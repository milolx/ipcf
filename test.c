#include "split.h"

/*
void proc_timer(void);
int upper_send(u8 dmac, u8 smac, void *msg, int msglen);
int upper_recv(u8 *buf, int *len);
int lower_fetch(u8 *buf, int *len);
int lower_put(void *raw_frm, int rawlen);
void split_init();
void split_cleanup();
*/

int main()
{
	u8 s[] = "abcdefg";
	u8 r[sizeof s];
	u8 low[sizeof s];
	int len;

	split_init();
	printf("init\n");
	upper_send(2, 1, s, sizeof s);
	printf("send\n");
	len = sizeof low;
	lower_fetch(low, &len);
	printf("lfetch\n");
	lower_put(low, sizeof low);
	printf("lput\n");
	len = sizeof r;
	upper_recv(r, &len);
	printf("recv\n");

	split_cleanup();

	return 0;
}
