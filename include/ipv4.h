#ifndef __IPV4_H__
#define __IPV4_H__

#include "config.h"
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

#endif /* __IPV4_H__ */
