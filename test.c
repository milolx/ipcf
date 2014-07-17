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

#define TOT_SIZE	(50*1000*1000)
#define GEN_ERR		1
//#undef GEN_ERR
//#define VERBOSE	1

#ifdef EVALUATION
extern unsigned long long e_valid;
extern unsigned long long e_tot;
#endif

int main()
{
	u8 s[LINK_DATA_MAX];
	u8 r[sizeof s];
	u8 low[LINK_MTU];
	int len;
	int ret;
	int n;
	int fail = 0;
	bool again = true;
	unsigned long long tot = 0;

	split_init();

	while (tot < TOT_SIZE) {
		// generate a random length of a packet[0..LINK_DATA_MAX]
		n = random_range(LINK_DATA_MAX);
		if (!n)
			continue;
		if (tot + n > TOT_SIZE)
			n = TOT_SIZE - tot;
		// generate the pkt of length n
		random_bytes(s, n);
#ifdef VERBOSE
		printf("-------------\n");

		printf("\nsend buf\n");
		//hex_dump(stdout, s, n, 0, true);
#endif
		n = upper_send(2, 1, s, n);
		tot += n;
		printf("tot=%llu\n", tot);
		// the return value 'n' is the actural submitted length
#ifdef VERBOSE
		printf("%d bytes accepted\n", n);
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
			printf("ret=%d, len=%d, type=%d\n", ret, len, low[3]);
			//hex_dump(stdout, low, len, 0, true);
#endif
		//printf("%d %d\n", len, low[3]);
		//if (low[3] == 4 && len < 50)
		//	hex_dump(stdout, low, len, 0, true);
			if (!len)
				break;

#ifdef GEN_ERR
			// generate err
			x = len / 20; // err rate <= 5%
			//x = len / 80;
			if (x) {
				n_err = random_range(x);	// num of err
#ifdef VERBOSE
				printf("n_err=%d <------------\n", n_err);
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
		if (!get_urq_num() && !get_lsq_num())
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
		//hex_dump(stdout, r, len, 0, true);
#endif
		if (again && !len) {
			++ fail;
#ifdef VERBOSE
			printf("len=0, fail=%d\n", fail);
#endif
			continue;
		}

		if (memcmp(s, r, n)) {
			printf("orig(%d)---------------\n", n);
			hex_dump(stdout, s, n, 0, true);
			printf("err(%d)---------------\n", len);
			hex_dump(stdout, r, len, 0, true);
			printf("#####\n");
			++ fail;
#ifdef VERBOSE
			printf("mismatch, fail=%d\n", fail);
#endif
		}
		else {
			// "ack all" may lost
			if (again) {
				proc_timer();
				if (get_lsq_num() > 0) {
					again = false;
					goto retry;
				}
			}
			again = true;
		}
	}
	printf("\n\nfail=%d\n", fail);
#ifdef EVALUATION
	printf("valid=%llu, tot=%llu, ratio=%f\n", e_valid, e_tot, ((float)e_valid)/((float)e_tot));
#endif

	split_cleanup();

	return 0;
}
