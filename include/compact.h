#ifndef __COMPACT_H__
#define __COMPACT_H__

#include "config.h"
#include "types.h"
#include "ipv4.h"
#include "list.h"

#define MAX_DATA_LENGTH		2000
#define SOFT_TIMEOUT_INTERVAL	(10*1000)	// in msec
#define N_ID_BITS		8

typedef struct {
	u32 saddr;
	u32 daddr;
	u16 ipid;
	u8  proto;
	u8  resv;
	u16 sport;
	u16 dport;
}skey_t;

typedef struct {
	u8 smac;
	u8 id;
}rkey_t;

#define CTYPE_FRM_CTL		0
#define CTYPE_FRM_TCP		1
#define CTYPE_FRM_UDP		2
#define CTYPE_FRM_RAW		3
#define CTYPE_CTL_REQ		1
#define CTYPE_CTL_ACK		2
#define CTYPE_CTL_FLT		3
#define CTYPE_TCP_SBIT		0x20	// has seq
#define CTYPE_TCP_ABIT		0x10	// has ack
#define CTYPE_TCP_WBIT		0x08	// has window size
#define CTYPE_TCP_URG		0x0
#define CTYPE_TCP_SYN		0x1
#define CTYPE_TCP_FIN		0x2
#define CTYPE_TCP_RST		0x3
#define CTYPE_TCP_PSH		0x4
#define CTYPE_TCP_URGSYN	0x5
#define CTYPE_TCP_URGFIN	0x6
#define CTYPE_TCP_URGPSH	0x7
#define CTYPE_TCP_SBMASK	0x7
#define CTYPE_RAW_FBIT		0x20	// fragment
#define CTYPE_RAW_MFBIT		0x10	// more fragment
#define CTYPE_RAW_IDMASK	0xf
typedef struct {
#if defined(__LITTLE_ENDIAN__)
	u8 i:6,
	   t:2;
#elif defined(__BIG_ENDIAN__)
	u8 t:2,
	   i:6;
#else
#error "__LITTLE_ENDIAN__ or __BIG_ENDIAN__ must be defined"
#endif
	u8 smac;
	u8 id;
}__attribute__((packed)) chdr_t;

struct orig_hdr {
	struct iphdr ip;
	union {
		struct tcphdr tcp;
		struct udphdr udp;
	}t;
};

typedef struct {
	struct list node;
	u16 len;
	u8 data[MAX_DATA_LENGTH];
}pkt_t;

#endif /* __COMPACT_H__ */
