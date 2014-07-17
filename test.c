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

#define NUM_OF_TESTS	10000
#define GEN_ERR		1
//#define VERBOSE	1

#ifdef EVALUATION
extern unsigned long long e_valid;
extern unsigned long long e_tot;
#endif

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
	u8 data1[]= {0x7e, 0x03, 0x91, 0x35, 0x0d, 0xa8, 0x0e, 0x4b,
	             0x5d, 0xd9, 0x10, 0xd1, 0xaa, 0x77, 0x54, 0x3d,
		     0x90, 0x9c, 0xe9, 0xe9, 0x64, 0xfc, 0xe5, 0xd1,
		     0x66, 0x4a, 0xc8, 0x00, 0x1d, 0x9b, 0x0a, 0xb4,
		     0x87, 0x5b, 0xf5, 0xe6};
	u8 data2[]= {0x7e, 0x03, 0x91, 0x35, 0x0d, 0xa8, 0x0e, 0x4b,
	             0x5d, 0xd5, 0x10, 0xd4, 0xaa, 0x77, 0x54, 0x3d,
		     0x90, 0x9c, 0xe9, 0xe9, 0x64, 0xfc, 0xe5, 0xd1,
		     0x66, 0x4a, 0xc8, 0x00, 0x1d, 0x9b, 0x0a, 0xb4,
		     0x87, 0x5b, 0xf5, 0xe6};
	u8 data3[]= {0x7e, 0x03, 0x91, 0x35, 0x0d, 0xa8, 0x0e, 0x4b,
	             0x5d, 0xd9, 0x10, 0xd1, 0xaa, 0x77, 0x54, 0x3d,
		     0x90, 0x9c, 0xe9, 0xe9, 0x64, 0xfc, 0xe5, 0xd1,
		     0x66, 0x4a, 0xc8, 0x00, 0x1d, 0x9b, 0x0a, 0xb4,
		     0x87, 0x5b, 0, 0};
	u8 data4[]= {0x7e, 0x03, 0x91, 0x35, 0x0d, 0xa8, 0x0e, 0x4b,
	             0x5d, 0xd5, 0x10, 0xd4, 0xaa, 0x77, 0x54, 0x3d,
		     0x90, 0x9c, 0xe9, 0xe9, 0x64, 0xfc, 0xe5, 0xd1,
		     0x66, 0x4a, 0xc8, 0x00, 0x1d, 0x9b, 0x0a, 0xb4,
		     0x87, 0x5b, 0, 0};

	printf("%04x\n", crc16(0, data3, sizeof data3));
	printf("%04x\n", crc16(0, data4, sizeof data4));

	split_init();

	for (i=0; i<NUM_OF_TESTS; ++i) {
		// generate a random length of a packet[0..LINK_DATA_MAX]
		n = random_range(LINK_DATA_MAX);
		if (!n)
			continue;
		// generate the pkt of length n
		random_bytes(s, n);
#ifdef VERBOSE
		printf("-------------\n");
		printf("init, n=%d\n", n);

		printf("\nsend buf\n");
		hex_dump(stdout, s, n, 0, true);
#endif
		n = upper_send(2, 1, s, n);
		// the return value 'n' is the actural submitted length
#ifdef VERBOSE
		printf("n=%d\n", n);
#endif

retry:
		do {
			int n_err;
			int j;
			int pos;
			int x;

#ifdef VERBOSE
			printf("\nlfetch & lput\n");
#endif
			len = sizeof low;
			ret = lower_fetch(low, &len);
#ifdef VERBOSE
			printf("ret=%d, len=%d\n", ret, len);
			hex_dump(stdout, low, len, 0, true);
#endif
			if (!len)
				break;

#ifdef GEN_ERR
			// generate err
			//x = len / 20; // err rate <= 5%
			x = len / 80;
			if (x) {
				n_err = random_range(x);	// num of err
#ifdef VERBOSE
				printf("len=%d, n_err=%d <------------\n", len, n_err);
#endif
				for (j=0; j < n_err; ++j) {
					pos = random_range(len);// random pos
#ifdef VERBOSE
					printf("pos=%02x, %02x->", pos, low[pos]);
#endif
					low[pos] = random_uint8();
#ifdef VERBOSE
					printf("%02x\n", low[pos]);
#endif
				}
			}
#endif

			lower_put(low, len);
		} while(1);

		// simulate timer working proc
		proc_timer();
		if (get_lsq_num() > 0) {
			// if timer gen some pkts
#ifdef VERBOSE
			printf("timeout...and try again\n");
#endif
			goto retry;
		}

#ifdef VERBOSE
		printf("\nrecv\n");
#endif
		len = sizeof r;
		ret = upper_recv(r, &len);
#ifdef VERBOSE
		printf("ret=%d, len=%d\n", ret, len);
		hex_dump(stdout, r, len, 0, true);
#endif

		if (memcmp(s, r, n)) {
			printf("orig---------------\n");
			hex_dump(stdout, s, n, 0, true);
			printf("err ---------------\n");
			hex_dump(stdout, r, n, 0, true);
			printf("#####\n");
			++ fail;
		}
	}
	printf("\n\nfail=%d\n", fail);
#ifdef EVALUATION
	printf("valid=%llu, tot=%llu, ratio=%f\n", e_valid, e_tot, ((float)e_valid)/((float)e_tot));
#endif

	split_cleanup();

	return 0;
}
