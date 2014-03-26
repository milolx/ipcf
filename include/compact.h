#ifndef __COMPACT_H__
#define __COMPACT_H__

#include "types.h"

struct iphdr {
#if defined(__LITTLE_ENDIAN__)
	u8 ihl:4,
	   version:4;
#elif defined(__BIG_ENDIAN__)
	u8 version:4,
	   ihl:4;
#else
#error "__LITTLE_ENDIAN__ or __BIG_ENDIAN__ must be defined"
#endif
	u8  tos;
	u16 tot_len;
	u16 id;
#define FRAG_FLAG_DF_MASK	0x4000
#define FRAG_FLAG_MF_MASK	0x2000
#define FRAG_OFF_MASK		0x1fff
	u16 frag_off;
	u8  ttl;
#define PROTOCOL_TCP		0x06
#define PROTOCOL_UDP		0x11
	u8  protocol;
	u16 check;
	u32 saddr;
	u32 daddr;
	/*The options start here. */
};

struct tcphdr {
	u16 source;
	u16 dest;
	u32 seq;
	u32 ack_seq;
#if defined(__LITTLE_ENDIAN__)
	u16 res1:4,
	    doff:4,
	    fin:1,
	    syn:1,
	    rst:1,
	    psh:1,
	    ack:1,
	    urg:1,
	    ece:1,
	    cwr:1;
#elif defined(__BIG_ENDIAN__)
	u16 doff:4,
	    res1:4,
	    cwr:1,
	    ece:1,
	    urg:1,
	    ack:1,
	    psh:1,
	    rst:1,
	    syn:1,
	    fin:1;
#else
#error "__LITTLE_ENDIAN__ or __BIG_ENDIAN__ must be defined"
#endif
	u16 window;
	u16 check;
	u16 urg_ptr;
};

struct udphdr {
	u16 source;
	u16 dest;
	u16 len;
	u16 check;
};

struct {
	u32 saddr,
	u32 daddr,
	u16 sport,
	u16 dport,
	u8  proto,
}__attribute__((packed)) key_t;

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
#define CTYPE_TCP_URG&PSH	0x7
#define CTYPE_RAW_FBIT		0x20	// fragment
#define CTYPE_RAW_MFBIT		0x10	// more fragment
#define CTYPE_RAW_IDMASK	0xf
typedef struct {
#if defined(__LITTLE_ENDIAN__)
	u8 i:6,
	u8 t:2,
#elif defined(__BIG_ENDIAN__)
	u8 t:2,
	u8 i:6,
#else
#error "__LITTLE_ENDIAN__ or __BIG_ENDIAN__ must be defined"
#endif
	u8 smac,
	u8 id,
}__attribute__((packed)) chdr_t;

struct pkt_orig {
	struct iphdr ip,
	union {
		struct tcphdr tcp,
		struct udphdr udp,
	}t,
};

#endif /* __COMPACT_H__ */