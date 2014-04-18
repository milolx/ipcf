#ifndef __SPLIT_H__
#define __SPLIT_H__

#define BREAK_CHAR	0xc0
#define ESCAPE_CHAR	0xdb
#define ESCAPE_BREAK	0xdc
#define ESCAPE_ESCAPE	0xdb

typedef struct {
	u8 src;
	u8 dst;
	u16 len;
#define FRAME_TYPE_DATA	1
#define FRAME_TYPE_ACKA	2
#define FRAME_TYPE_ACKR	3
#define FRAME_TYPE_ACKP	4
	u8 type;
	u8 no;
	u16 csum;
	u8 data[0];
}frm_hdr_t;

typedef struct {
#define SLICE_SEPERATOR	0x7e
	u8 sep;
	u8 seq;
	u8 data[SLICE_DATA_LEN];
	u16 csum;
}slice_hdr_t;

#endif /* __SPLIT_H__ */
