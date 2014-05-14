#include "split.h"
#include "rand.h"
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

int main()
{
	//u8 s[] = "";
	//u8 s[] = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
	u8 crc;
	u8 s[LINK_DATA_MAX];
	u8 r[sizeof s];
	u8 low[LINK_MTU];
	int len;
	int ret;
	int n;
	int fail = 0;
	int i;

	split_init();

	for (i=0; i<1000; ++i) {
		n = random_range(LINK_DATA_MAX);
		if (!n)
			continue;
		random_bytes(s, n);
		printf("-------------\n");
		printf("init, n=%d\n", n);

		printf("\nsend\n");
		hex_dump(stdout, s, n, 0, true);
		n = upper_send(2, 1, s, n);
		printf("n=%d\n", n);

retry:
		do {
			int n_err;
			int j;
			int pos;
			int x;
			printf("\nlfetch & lput\n");
			len = sizeof low;
			ret = lower_fetch(low, &len);
			printf("ret=%d, len=%d\n", ret, len);
			hex_dump(stdout, low, len, 0, true);
			if (!len)
				break;

#if 1
			// generate err
			x = len / 20; // err rate <= 5%
			if (x) {
				n_err = random_range(x);
				printf("len=%d, n_err=%d <------------\n", len, n_err);
				for (j=0; j < n_err; ++j) {
					pos = random_range(len);	// random pos
					printf("pos=%d, %02x->", pos, low[pos]);
					low[pos] = random_uint8();
					printf("%02x\n", low[pos]);
				}
			}
#endif

			lower_put(low, len);
		} while(1);

		proc_timer();
		if (get_lsq_num() > 0) {
			printf("timeout...and try again\n");
			goto retry;
		}

		printf("\nrecv\n");
		len = sizeof r;
		ret = upper_recv(r, &len);
		printf("ret=%d, len=%d\n", ret, len);
		hex_dump(stdout, r, len, 0, true);

		if (memcmp(s, r, n)) {
			++ fail;
			break;
		}
	}
	printf("\n\nfail=%d\n", fail);

	split_cleanup();

	return 0;
}
