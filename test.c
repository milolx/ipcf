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
	//u8 s[] = "";
	u8 s[] = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
	u8 r[sizeof s];
	u8 low[LINK_MTU];
	int len;
	int ret;

	split_init();
	printf("init\n");

	printf("\n\nsend\n");
	hex_dump(stdout, s, sizeof s, 0, true);
	upper_send(2, 1, s, sizeof s);

	printf("\n\nlfetch\n");
	len = sizeof low;
	ret = lower_fetch(low, &len);
	printf("ret=%d, len=%d\n", ret, len);
	hex_dump(stdout, low, len, 0, true);

	printf("\n\nlput\n");
	hex_dump(stdout, low, len, 0, true);
	lower_put(low, len);

	printf("\n\nrecv\n");
	len = sizeof r;
	ret = upper_recv(r, &len);
	printf("ret=%d\n", ret);
	hex_dump(stdout, r, len, 0, true);

	split_cleanup();

	return 0;
}
