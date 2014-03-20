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
	u16 frag_off;
	u8  ttl;
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

struct key {
	u32 saddr,
	u32 daddr,
	u16 sport,
	u16 dport,
	u8  proto,
}__attribute__((packed));

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
}__attribute__((packed)) chdr_common_t;

typedef struct {
#if defined(__LITTLE_ENDIAN__)
	u8 c:6,
	u8 t:2,
#elif defined(__BIG_ENDIAN__)
	u8 t:2,
	u8 c:6,
#else
#error "__LITTLE_ENDIAN__ or __BIG_ENDIAN__ must be defined"
#endif
	u8 smac,
	u8 id,
}__attribute__((packed)) cctl_hdr_t;


#define SPB_URG		0x0
#define SPB_SYN		0x1
#define SPB_FIN		0x2
#define SPB_RST		0x3
#define SPB_PSH		0x4
#define SPB_URGSYN	0x5
#define SPB_URGFIN	0x6
#define SPB_URGPSH	0x7

struct pkt_orig {
	struct iphdr ip,
	union {
		struct tcphdr tcp,
		struct udphdr udp,
	}t,
};

#endif /* __COMPACT_H__ */
