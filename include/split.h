#ifndef __SPLIT_H__
#define __SPLIT_H__

#include "types.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include "list.h"
#include "atomic.h"

#define NUM_OF_SE	256	// max num of r/s sessions, 8-bit mac as key
#define LINK_MTU	1000
#define SLICE_DATA_LEN	32
#define IDLE_TIMEOUT	(10 * 1000)	// in ms
//#define ACK_TIMEOUT	(3 * 1000)	// in ms
#define ACK_TIMEOUT	(0)		// 0 for debug
#define MAX_RETRY_TIMES	3

#define FRM_MSG_HDR_LEN	(sizeof(frm_hdr_t) + sizeof(msg_hdr_t))
#define LINK_DATA_MAX	(LINK_MTU - FRM_MSG_HDR_LEN)
// only max *possible* num of slices
#define MAX_SLICE_NUM	((LINK_MTU - FRM_MSG_HDR_LEN)/sizeof(slice_t))		// should <=256
#define BITMAP32_SIZE	((MAX_SLICE_NUM+31)/32)


#define BREAK_CHAR	0xc0
#define ESCAPE_CHAR	0xdb
#define ESCAPE_BREAK	0xdc
#define ESCAPE_ESCAPE	0xdb

typedef struct {
	u8 src;
	u8 dst;
	u8 type;
	u8 reserved;
	u16 len;
#define FRAME_TYPE_DATA		1
#define FRAME_TYPE_ACKA		2
#define FRAME_TYPE_ACKR		3
#define FRAME_TYPE_ACKP		4
#define FRAME_TYPE_PATCH	5
	u16 csum;
	u8 data[0];
}frm_hdr_t;
//}__attribute__((packed))frm_hdr_t;

typedef struct {
	u32 d_csum;
	u16 len;
	u16 len2;
	u8 seq;
	u8 csum;
	u8 data[0];
}__attribute__((packed))msg_hdr_t;
//}msg_hdr_t;
//}__attribute__((packed))msg_hdr_t;

typedef struct {
#define SLICE_SEPERATOR	0x7e
	u8 sep;
	u8 seq;
	u8 data[SLICE_DATA_LEN];
	u16 csum;
}slice_t;
//}__attribute__((packed))slice_t;

typedef struct {
	u8 mac;		// remote mac
}se_key_t;

typedef struct {
	struct list link;
	u8 n_slices;
	u8 frm[LINK_MTU];
	u16 esc_len;
	u8 escaped[LINK_MTU];
}send_node_t;

typedef struct {
	struct list link;
	u16 len;
	u8 data[LINK_MTU];
}data_node_t;

typedef struct {
	light_lock_t lock;
	long long int idle_timeout;
	long long int ack_timeout;
	int data_retry;
	int ack_retry;
	bool is_waiting;	// is waiting for ack?
	u8 seq;
	struct list pkt_list;
}sse_t;	//send session

typedef struct {
	light_lock_t lock;
	long long int idle_timeout;
	u32 bitmap[BITMAP32_SIZE];
	u8 seq_exp;
	u8 n_slices;	// recv session has only one data node,
			// so, we have n_slices here
	u16 msglen;
	u32 d_csum;
	u8 data[LINK_MTU];
	bool completed;
	bool nak;	// when set to true, force not ack all pkts
			// make the sender resend the whole frm
}rse_t;	//recv session

void proc_timer(void);
int upper_send(u8 dmac, u8 smac, void *msg, int msglen);
int upper_recv(u8 *buf, int *len);
int lower_fetch(u8 *buf, int *len);
int lower_put(void *raw_frm, int rawlen);
void split_init();
void split_cleanup();

#endif /* __SPLIT_H__ */
