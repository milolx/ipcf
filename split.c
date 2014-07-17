#include "atomic.h"
#include "split.h"
#include "ts.h"
#include "crc.h"
#include "dbg.h"

#ifdef __DEBUG__
#define __MILO_INFO_LEVEL__	0
#endif

#ifdef EVALUATION
unsigned long long e_valid = 0;
unsigned long long e_tot = 0;
#endif

sse_t *sse[NUM_OF_SE];	// send sessions, 8-bit key
light_lock_t sse_lock;
rse_t *rse[NUM_OF_SE];	// recv sessions, 8-bit key
light_lock_t rse_lock;

struct list upper_recv_list;
light_lock_t upper_recv_lock;

struct list lower_send_list;
light_lock_t lower_send_lock;

/*
 * return value:
 *   -1		frm size's too small
 * >= 0		actual length in frm
 * 
 * save to *data_len of actual length in data have been escaped
 */
static int en_frame(u8 *data, u16 *data_len, u8 *frm, u16 size)
{
	int len = -1;
	u8 *ch;
	u8 *p=frm;

	p = frm;
	ch = (u8 *)data;

	if (size < 1)
		goto err_out;
	*(p++) = BREAK_CHAR;

	for (ch = (u8 *)data; ch - data < *data_len; ++ch) {
		switch (*ch) {
			case BREAK_CHAR:
				if (p - frm + 2 > size)
					goto err_out;
				*(p++) = ESCAPE_CHAR;
				*(p++) = ESCAPE_BREAK;
				break;
			case ESCAPE_CHAR:
				if (p - frm + 2 > size)
					goto err_out;
				*(p++) = ESCAPE_CHAR;
				*(p++) = ESCAPE_ESCAPE;
				break;
			default:
				if (p - frm + 1 > size)
					goto err_out;
				*(p++) = *ch;
		}
	}

	if (p - frm + 1 > size)
		goto err_out;
	*(p++) = BREAK_CHAR;
	len = p - frm;

err_out:
	*data_len = ch - data;
	return len;
}

/*
 * return value:
 *   -1		frm/buf size's too small
 *   -2		escape followed by nothing
 *   -3		unknown escaped char
 * >= 0		actual length in buf
 */
static int de_frame(u8 *frm, u16 frm_len, u8 *buf, u16 *len)
{
	int ret=-1;
	u8 *ch;
	u8 *p;
	int find_start_break;

	p = buf;
	ch = frm;

	find_start_break = 0;
	while (ch-frm < frm_len) {
		if (!find_start_break && *ch != BREAK_CHAR) {
			++ ch;
			continue;
		}

		switch (*ch) {
			case BREAK_CHAR:
				if (!find_start_break)
					find_start_break = 1;
				else {
					ret = 0;
					goto out;	// success
				}
				break;
			case ESCAPE_CHAR:
				if (ch-frm+1 + 1 > frm_len) {
					ret = -2;
					goto out;	// err: end-up by
							//      escape
				}
				switch (*(++ch)) {
					case ESCAPE_BREAK:
						if (p-buf + 1 > *len)
							// err: no more root
							//      to store data
							goto out;
						*(p++) = BREAK_CHAR;
						break;
					case ESCAPE_ESCAPE:
						if (p-buf + 1 > *len)
							// err: no more root
							//      to store data
							goto out;
						*(p++) = ESCAPE_CHAR;
						break;
					default:
						// err: unknown escaped char
						ret = -3;
						goto out;
				}
				break;
			default:
				if (p-buf + 1 > *len)
					// err: no more room to store data
					goto out;
				*(p++) = *ch;
		}

		// parse next char
		++ch;
	}

out:
	*len = p - buf;
	return ret;
}

static u8 csum8(u8 old, void *d, int len)
{
	return crc8(old, d, len);
}

static u16 csum16(u16 old, void *d, int len)
{
	return crc16(old, d, len);
}

static void fill_slice_k(u8 k, void *from, u8 len, void *dest)
{
	slice_t *slice = (slice_t *)dest;

	// FIXME: what is this used for?
	slice->sep = SLICE_SEPERATOR;
	slice->seq = k;
	memcpy(slice->data, from, len);
	if (len < SLICE_DATA_LEN)
		bzero(slice->data + len, SLICE_DATA_LEN - len);
	slice->csum = 0;
	slice->csum = csum16(0, slice, sizeof *slice);
}

/*
 * start from 'msg' with length 'msglen' to 'dest' which size is 'size'
 */
static int fill_slices(void *dest, int size, void *msg, int *msglen)
{
	int n_slices = 0;
	int len, remain;
	slice_t *slice;
	void *data;
	u8 seq;

	for (
		slice = (slice_t *)dest, data = msg, remain=*msglen, seq=0;
		remain > 0 && ((void*)slice <= dest + size - SLICE_DATA_LEN);
		++slice, data += SLICE_DATA_LEN, ++seq 
	    ) {
		len = remain > SLICE_DATA_LEN ? SLICE_DATA_LEN:remain;
		fill_slice_k(seq, data, len, slice);
		remain -= len;
		n_slices = seq + 1;
	}
	*msglen -= remain;

	return n_slices;
}

static void set_bit(u32 *map, u8 k)
{
	u8 i;

	u8 offset = k>>5;	// div 32
	i = k & 0x1f;		// mod 32
	map[offset] |= 1L << i;
}

static void clr_bit(u32 *map, u8 k)
{
	u8 i;

	u8 offset = k>>5;	// div 32
	i = k & 0x1f;		// mod 32
	map[offset] &= ~(1L << i);
}

// find first zero pos start from k, but pos <= max
static int find_first_zero(u32 *map, u8 k, u8 max)
{
	u8 start = k>>5;
	u8 end = max>>5;
	u8 x, cnt;

	cnt = 0;
	x=start;
	while (x <= end && k < max) {
		if (!(map[x] & (1L<<(k & 0x1f))))
			return k;
		else {
			++cnt;
			if (++k & 0x1f  == 0)
				++x;
		}
	}
	return -1;
}

// check if all lower 'n' bits are all '1'
static bool test_all_bits_set(u32 *map, u8 n)
{
	u8 k = n>>5;		// div 32
	u8 t = n & 0x1f;	// mod 32
	u32 x;
	u8 i;

	for (i=0; i<k; ++i)
		if (~map[i] != 0)
			return false;
	if (t && map[k] != ( ~(((~0L) >> t)<<t) ))
		return false;

	return true;
}

static void clr_all_bits(rse_t *se)
{
	int i;
	//for (i=0; i<sizeof se->bitmap; ++i)
	for (i=0; i<BITMAP32_SIZE; ++i)
		se->bitmap[i] = 0;
}

static sse_t *get_sse(se_key_t *k)
{
	sse_t *se;

	_lock(&sse_lock);
	se = sse[k->mac];
	_unlock(&sse_lock);

	if (se)
		se->idle_timeout = ts_msec() + IDLE_TIMEOUT;

	return se;
}

static sse_t *remove_sse(se_key_t *k)
{
	sse_t *se;

	_lock(&sse_lock);
	se = sse[k->mac];
	if (se) {
		send_node_t *pkt,*next;

		sse[k->mac] = NULL;

		_lock_destroy(&se->lock);
		LIST_FOR_EACH_SAFE (pkt, next, link, &se->pkt_list) {
			list_remove(&pkt->link);
			free(pkt);
		}
		free(se);
	}
	_unlock(&sse_lock);
}

static sse_t *create_sse(se_key_t *k)
{
	sse_t *se = (sse_t *)xmalloc(sizeof *se);
	sse_t *oldse;
	int i;

	_lock_init(&se->lock);
	se->idle_timeout = ts_msec() + IDLE_TIMEOUT;
	se->ack_timeout = -1;
	se->retry = 0;
	se->is_waiting = false;
	se->seq = 0;	// for stop-wait protocol, just 0 or 1
	list_init(&se->pkt_list);

	_lock(&sse_lock);
	oldse=sse[k->mac];
	sse[k->mac] = se;
	_unlock(&sse_lock);
	if (oldse) {
		send_node_t *pkt,*next;

#if __MILO_INFO_LEVEL__ >= 1
		DBG("(create sse)sse[key]'s not empry\n");
#endif
		_lock(&sse_lock);

		_lock_destroy(&oldse->lock);
		LIST_FOR_EACH_SAFE (pkt, next, link, &oldse->pkt_list) {
			list_remove(&pkt->link);
			free(pkt);
		}
		free(oldse);

		_unlock(&sse_lock);
	}

	return se;
}

static rse_t *get_rse(se_key_t *k)
{
	rse_t *se;

	_lock(&rse_lock);
	se = rse[k->mac];
	_unlock(&rse_lock);

	if (se)
		se->idle_timeout = ts_msec() + IDLE_TIMEOUT;

	return se;
}

static rse_t *remove_rse(se_key_t *k)
{
	rse_t *se;

	_lock(&rse_lock);
	se = rse[k->mac];
	if (se) {
		rse[k->mac] = NULL;

		_lock_destroy(&se->lock);
		free(se);
	}
	_unlock(&rse_lock);
}

static void reset_rse(rse_t *se)
{
	int i;

	if (se) {
		se->idle_timeout = ts_msec() + IDLE_TIMEOUT;
		clr_all_bits(se);
		se->seq_exp = 0xff;	// in fact, i dont know
		se->n_slices = 0;
		se->msglen = 0;
		bzero(se->data, sizeof se->data);
		se->completed = false;
	}
}

static rse_t *create_rse(se_key_t *k)
{
	rse_t *se = (rse_t *)xmalloc(sizeof *se);
	rse_t *oldse;
	int i;

	_lock_init(&se->lock);
	reset_rse(se);

	_lock(&rse_lock);
	oldse=rse[k->mac];
	rse[k->mac] = se;
	_unlock(&rse_lock);
	if (oldse) {
#if __MILO_INFO_LEVEL__ >= 1
		DBG("(create rse)rse[key]'s not empry\n");
#endif
		_lock(&rse_lock);

		_lock_destroy(&oldse->lock);
		free(oldse);

		_unlock(&rse_lock);
	}

	return se;
}

static void put_upper_recv_list(u8 *buf, int len)
{
	data_node_t *s = (data_node_t *)xmalloc(sizeof *s);

	s->len = len <= sizeof s->data ? len:sizeof s->data;
	memcpy(s->data, buf, s->len);
	_lock(&upper_recv_lock);
	list_push_back(&upper_recv_list, &s->link);
	_unlock(&upper_recv_lock);
}

static void put_lower_send_list(u8 *buf, int len)
{
	data_node_t *s = (data_node_t *)xmalloc(sizeof *s);

	s->len = len <= sizeof s->data ? len:sizeof s->data;
	memcpy(s->data, buf, s->len);
	_lock(&lower_send_lock);
	list_push_back(&lower_send_list, &s->link);
	_unlock(&lower_send_lock);
}

static void try_lower_send(sse_t *se)
{
	send_node_t *pkt;
	struct list *node;

	if (!se->is_waiting && !list_is_empty(&se->pkt_list)) {
		node = list_front(&se->pkt_list);
		ASSIGN_CONTAINER(pkt, node, link);
		put_lower_send_list(pkt->escaped, pkt->esc_len);
		// set ack-timer
		se->ack_timeout = ts_msec() + ACK_TIMEOUT;
		se->retry ++;
		se->is_waiting = true;
	}
}

static void cp_slice_to_rse(rse_t *se, slice_t *s)
{
	u8 len;
	u16 csum_save;

	csum_save = s->csum;
	s->csum = 0;
	if (csum_save != csum16(s->csum, s, sizeof *s)) {
#if __MILO_INFO_LEVEL__ >= 3
		DBG("(cp slice to rse)slice csum err\n");
#endif
		return;
	}

	if (s->seq > se->n_slices) {
#if __MILO_INFO_LEVEL__ >= 1
		DBG("(cp slice to rse)invalid seq(%d, max=%d)\n",
				s->seq, se->n_slices);
#endif
		return;
	}

	len = SLICE_DATA_LEN;
	if (s->seq == se->n_slices - 1)
		if (se->msglen % SLICE_DATA_LEN)
			len = se->msglen % SLICE_DATA_LEN;
	memcpy(se->data + SLICE_DATA_LEN*s->seq, s->data, len);
	set_bit(se->bitmap, s->seq);
}

static void patch_rse(rse_t *se, slice_t *slice, u8 n_slices)
{
	int i;

	for (i=0; i<n_slices; ++slice, ++i)
		cp_slice_to_rse(se, slice);
}

static void send_ack(u8 dst, u8 src, rse_t *se)
{
	u16 esc_len;
	u8 escaped[LINK_MTU];

	u8 buf[LINK_MTU];
	frm_hdr_t *fh = (frm_hdr_t *)buf;
	msg_hdr_t *mh = (msg_hdr_t *)fh->data;
	u8 *data = mh->data;

	// FIXME: normally, dmac in the first place
	fh->src = src;
	fh->dst = dst;
	if (test_all_bits_set(se->bitmap, se->n_slices))
		fh->type = FRAME_TYPE_ACKA;
	else
		fh->type = FRAME_TYPE_ACKP;

	mh->seq = se->seq_exp;
	mh->len = 0;
	if (fh->type == FRAME_TYPE_ACKP) {
		int result;
		u8 k;

		k = 0;
		while (1) {
			result = find_first_zero(se->bitmap, k, se->n_slices);
			if (result < 0)
				break;
			k = result;
			*(data++) = k++;
			++ mh->len;
		}
	}
	mh->csum = 0;
	// FIXME: this is urgly...
	//        violate priciple of protocol layering
	mh->csum = csum8(0, mh, sizeof *mh + mh->len);

	fh->len = FRM_MSG_HDR_LEN + mh->len;
	fh->csum = 0;
	fh->csum = csum8(0, fh, sizeof *fh);

	esc_len = en_frame(buf, &fh->len, escaped, LINK_MTU);
	if (esc_len < 0) {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("(send ack)escaped length exceed MTU\n");
#endif
	}
	else
		put_lower_send_list(escaped, esc_len);
}

static void proc_data(void *buf, int len)
{
	frm_hdr_t *fh = (frm_hdr_t *)buf;
	msg_hdr_t *mh = (msg_hdr_t *)fh->data;
	slice_t *slice = (slice_t *)mh->data;
	se_key_t key;
	u8 n_possible;
	u8 n_slices;
	rse_t *se;
	u8 csum_save;

	if (len - sizeof *fh < sizeof *mh) {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("(proc data)frm data's too short(%d)\n", len-sizeof *fh);
#endif
		return;
	}
	csum_save = mh->csum;
	mh->csum = 0;
	if (csum_save != csum8(mh->csum, mh, sizeof *mh)) {
#if __MILO_INFO_LEVEL__ >= 3
		DBG("(proc data)msg csum err\n");
#endif
		return;
	}

	// in FRAME_TYPE_DATA frame, mh->len hold the actual length of data

	// according to the de-frame length, n_possible is the possible max
	// num of slices
	n_possible = (len - FRM_MSG_HDR_LEN) / sizeof *slice;
	// according to the msg hdr's length indication (additional zeros at
	// the tail is possible), n_slices is the num of slices
	n_slices = (mh->len + SLICE_DATA_LEN - 1)/SLICE_DATA_LEN;
#if 0
	if (s < t) {
#if __MILO_INFO_LEVEL__ >= 1
		DBG("(proc data)msg length invalid(%d, frm len=%d)\n",
				mh->len, fh->len);
#endif
		return;
	}
#endif
	if (n_slices > MAX_SLICE_NUM)
		n_slices = MAX_SLICE_NUM;

	key.mac = fh->src;
	se = get_rse(&key);

	if (se) {
		_lock(&se->lock);
		if (se->completed) {
			if (se->seq_exp != mh->seq) {
				// a new message trans
				clr_all_bits(se);
				se->seq_exp = mh->seq;
				se->msglen = mh->len;
				se->n_slices = n_slices;
				se->completed = false;
			}
			else {
				// duplitcated data
			}
		}
		else {
#if __MILO_INFO_LEVEL__ >= 1
			DBG("(proc data)need patch, data received..."	\
					"ack may lost! reset\n");
#endif
			reset_rse(se);

			se->seq_exp = mh->seq;
			se->msglen = mh->len;
			se->n_slices = n_slices;
		}
	}
	if (!se) {
		se = create_rse(&key);

		_lock(&se->lock);
		se->seq_exp = mh->seq;
		se->msglen = mh->len;
		se->n_slices = n_slices;
		se->completed = false;
	}
	patch_rse(se, (slice_t *)mh->data, n_possible);
	send_ack(fh->src, fh->dst, se);

	if (test_all_bits_set(se->bitmap, n_slices)) {
		// ensure submit only once
		if (!se->completed) {
			put_upper_recv_list(se->data, se->msglen);
			se->completed = true;
		}
	}
	_unlock(&se->lock);
}

static void proc_ack_req(void *buf, int len)
{
	frm_hdr_t *fh = (frm_hdr_t *)buf;
	msg_hdr_t *mh = (msg_hdr_t *)fh->data;
	se_key_t key;
	rse_t *se;
	u8 csum_save;

	if (len - sizeof *fh < sizeof *mh) {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("(proc ack req)frm data's too short(%d)\n",
				len - sizeof *fh);
#endif
		return;
	}
	csum_save = mh->csum;
	mh->csum = 0;
	if (csum_save != csum8(mh->csum, mh, sizeof *mh)) {
#if __MILO_INFO_LEVEL__ >= 3
		DBG("(proc ack req)msg csum err\n");
#endif
		return;
	}

	key.mac = fh->src;
	se = get_rse(&key);

	if (se) {
		_lock(&se->lock);
		if (se->seq_exp == mh->seq) {
			send_ack(fh->src, fh->dst, se);
		}
		else {
#if __MILO_INFO_LEVEL__ >= 2
			u8 exp = se->seq_exp;
#endif
			_unlock(&se->lock);
#if __MILO_INFO_LEVEL__ >= 2
			DBG("req ack seq is not expected"		\
					"(exp:%d, get:%d), ignore\n",
					exp, mh->seq);
#endif
			return;
		}
		_unlock(&se->lock);
	}
	else {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("rse's not found(smac:0x%02x)\n", fh->src);
#endif
		return;
	}
}

static void proc_patch(void *buf, int len)
{
	frm_hdr_t *fh = (frm_hdr_t *)buf;
	msg_hdr_t *mh = (msg_hdr_t *)fh->data;
	slice_t *slice = (slice_t *)mh->data;
	se_key_t key;
	u8 n_slices;
	rse_t *se;
	u8 csum_save;

	if (len - sizeof *fh < sizeof *mh) {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("(proc patch)frm data's too short(%d)\n",
				len - sizeof *fh);
#endif
		return;
	}
	csum_save = mh->csum;
	mh->csum = 0;
	if (csum_save != csum8(mh->csum, mh, sizeof *mh)) {
#if __MILO_INFO_LEVEL__ >= 3
		DBG("(proc patch)msg csum err\n");
#endif
		return;
	}

	// in FRAME_TYPE_PATCH frame, mh->len hold n_slices
	// calculate max possible num of slices first
	n_slices = (len - FRM_MSG_HDR_LEN) / sizeof *slice;
	if (n_slices > mh->len)
		n_slices = mh->len;

	key.mac = fh->src;
	se = get_rse(&key);

	//	cmp	seq
	//	y	y	dup
	//	y	n	err
	//	n	y	proc
	//	n	n	err
	if (se) {
		_lock(&se->lock);
		if (se->seq_exp != mh->seq) {
#if __MILO_INFO_LEVEL__ >= 2
			u8 exp=se->seq_exp;
#endif
			_unlock(&se->lock);
#if __MILO_INFO_LEVEL__ >= 2
			DBG("(proc patch)recv seq is not expected"	\
					"(exp:%d, get:%d), ignore\n",	\
					exp, mh->seq);
#endif
			return;
		}
		else {
			if (!se->completed) {
				patch_rse(se, slice, n_slices);
				send_ack(fh->src, fh->dst, se);

				if (test_all_bits_set(se->bitmap,
							se->n_slices)) {
					put_upper_recv_list(se->data,
							se->msglen);
					se->completed = true;
				}
			}
			else {
				// duplitcated data
				_unlock(&se->lock);
#if __MILO_INFO_LEVEL__ >= 2
				DBG("(proc patch)duplicated pkt");
#endif
				return;
			}
		}
		_unlock(&se->lock);
	}
	else {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("(proc patch)patch sse doesn't exist\n");
#endif
		return;
	}
}

static void proc_ack_all(void *buf, int len)
{
	frm_hdr_t *fh = (frm_hdr_t *)buf;
	msg_hdr_t *mh = (msg_hdr_t *)fh->data;
	se_key_t key;
	sse_t *se;
	u8 csum_save;

	if (len - sizeof *fh < sizeof *mh) {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("(proc ack all)frm data's too short(%d)\n",
				len - sizeof *fh);
#endif
		return;
	}
	csum_save = mh->csum;
	mh->csum = 0;
	if (csum_save != csum8(mh->csum, mh, sizeof *mh)) {
#if __MILO_INFO_LEVEL__ >= 3
		DBG("(proc ack all)msg csum err\n");
#endif
		return;
	}

	key.mac = fh->src;
	// read only, no lock required
	se = get_sse(&key);

	if (se) {
		// this lock is a little big...
		// the aim here is to make 'test empty' and 'pop node' atomic
		_lock(&se->lock);
		if (!list_is_empty(&se->pkt_list)) {
			struct list *list_node;
			send_node_t *pkt;
			frm_hdr_t *fh_;
			msg_hdr_t *mh_;

			list_node = list_front(&se->pkt_list);
			ASSIGN_CONTAINER(pkt, list_node, link);

			fh_ = (frm_hdr_t *)pkt->frm;
			if (fh_->src != fh->dst || fh_->dst != fh->src) {
				_unlock(&se->lock);
#if __MILO_INFO_LEVEL__ >= 2
				DBG("(proc ack all)mac addr"		\
						" mismatch, ignore\n");
#endif
				return;
			}
			mh_ = (msg_hdr_t *)fh_->data;
			if (mh_->seq == mh->seq) {
				list_pop_front(&se->pkt_list);

				se->is_waiting = false;
				se->ack_timeout = -1;	// remove ack-timer
				se->retry = 0;
				try_lower_send(se);

				_unlock(&se->lock);

				free(pkt);
			}
			else {
#if __MILO_INFO_LEVEL__ >= 2
				u8 exp = mh_->seq;
#endif
				_unlock(&se->lock);
#if __MILO_INFO_LEVEL__ >= 2
				DBG("(proc ack all)ack seq is not"	\
						" expected(exp:%d,"	\
						" get:%d), ignore\n",
						exp, mh->seq);
#endif
				return;
			}
		}
		else {
			_unlock(&se->lock);
#if __MILO_INFO_LEVEL__ >= 2
			DBG("(proc ack all)send q's empty\n");
#endif
			return;
		}
	}
	else {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("(proc ack all)sse's not found\n");
#endif
		return;
	}
}

static void send_patch(send_node_t *pkt, u8 *data, u16 len)
{
	u8 tmp[LINK_MTU];
	frm_hdr_t *tmpfh = (frm_hdr_t *)tmp;
	msg_hdr_t *tmpmh = (msg_hdr_t *)tmpfh->data;
	slice_t *tmpslice;
	int i;

	frm_hdr_t *fh = (frm_hdr_t *)pkt->frm;
	msg_hdr_t *mh = (msg_hdr_t *)fh->data;
	slice_t *slice = (slice_t *)mh->data;

	tmpfh->src = fh->src;
	tmpfh->dst = fh->dst;
	tmpfh->type = FRAME_TYPE_PATCH;
	tmpmh->seq = mh->seq;
	tmpmh->len = 0;
	for (
		i=0, tmpslice = (slice_t *)tmpmh->data;
		i<len && (((u8 *)tmpslice) < tmp + LINK_MTU - sizeof *slice);
		++i, ++tmpslice
	    ) {
		if (data[i] < pkt->n_slices) {
			// anyway, copy the whole slice
			memcpy(tmpslice, slice+data[i], sizeof *slice);
			++ tmpmh->len;
		}
		else {
#if __MILO_INFO_LEVEL__ >= 2
			DBG("(send patch)need seq(%d) out of"	\
					" range(<%d), skip\n",
					data[i], pkt->n_slices);
#endif
			// skip
		}
	}
	tmpmh->csum = 0;
	tmpmh->csum = csum8(0, tmpmh, sizeof *tmpmh);

	tmpfh->len = FRM_MSG_HDR_LEN + sizeof(slice_t)*tmpmh->len;
	tmpfh->csum = 0;
	tmpfh->csum = csum8(0, tmpfh, sizeof *tmpfh);

	pkt->esc_len = en_frame(tmp, &tmpfh->len, pkt->escaped, LINK_MTU);
	if (pkt->esc_len < 0) {
#if __MILO_INFO_LEVEL__ >= 1
		DBG("(send patch)escaped length exceed MTU\n");
#endif
	}
	else
		put_lower_send_list(pkt->escaped, pkt->esc_len);
}

static void proc_ack_part(void *buf, int len)
{
	frm_hdr_t *fh = (frm_hdr_t *)buf;
	msg_hdr_t *mh = (msg_hdr_t *)fh->data;
	se_key_t key;
	sse_t *se;
	u8 csum_save;

	if (len - sizeof *fh < sizeof *mh) {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("(proc ack part)frm data's too short(%d)\n",
				len - sizeof *fh);
#endif
		return;
	}
	if (mh->len != len - FRM_MSG_HDR_LEN) {
#if __MILO_INFO_LEVEL__ >= 3
		DBG("(proc ack part)msg len invalid\n");
#endif
		return;
	}
	csum_save = mh->csum;
	mh->csum = 0;
	if (csum_save != csum8(mh->csum, mh, sizeof *mh + mh->len)) {
#if __MILO_INFO_LEVEL__ >= 3
		DBG("(proc ack part)msg csum err\n");
#endif
		return;
	}

	if (mh->len > len - FRM_MSG_HDR_LEN) {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("(proc ack part)msg len invaid"		\
				"(%d, frm len=%d)\n", mh->len, len);
#endif
		return;
	}

	key.mac = fh->src;
	se = get_sse(&key);

	if (se) {
		// reference to proc_ack_all
		_lock(&se->lock);
		if (!list_is_empty(&se->pkt_list)) {
			struct list *list_node;
			send_node_t *pkt;
			frm_hdr_t *fh_;
			msg_hdr_t *mh_;

			list_node = list_front(&se->pkt_list);
			ASSIGN_CONTAINER(pkt, list_node, link);

			fh_ = (frm_hdr_t *)pkt->frm;
			if (fh_->src != fh->dst || fh_->dst != fh->src) {
				_unlock(&se->lock);
#if __MILO_INFO_LEVEL__ >= 2
				DBG("(proc ack part)mac addr"	\
						" mismatch, ignore\n");
#endif
				return;
			}
			mh_ = (msg_hdr_t *)fh_->data;
			if (mh_->seq != mh->seq) {
#if __MILO_INFO_LEVEL__ >= 2
				u8 exp = mh_->seq;
#endif
				_unlock(&se->lock);
#if __MILO_INFO_LEVEL__ >= 2
				DBG("(proc ack part)ack seq is not"	\
						" expected(exp:%d,"	\
						" get:%d), ignore\n",
						exp, mh->seq);
#endif
				return;
			}
			// reset ack-timer
			se->ack_timeout = ts_msec() + ACK_TIMEOUT;
			se->retry = 0;
			send_patch(pkt, mh->data, mh->len);
			// FIXME: should i make the lock smaller by clone 'pkt'?
			_unlock(&se->lock);
		}
		else {
			_unlock(&se->lock);
#if __MILO_INFO_LEVEL__ >= 2
			DBG("(proc ack part)send q's empty\n");
#endif
			return;
		}
	}
	else {
#if __MILO_INFO_LEVEL__ >= 2
		DBG("(proc ack part)sse's not found\n");
#endif
		return;
	}
}

#ifdef __DEBUG__
int get_urq_num()
{
	int n;
	_lock(&upper_recv_lock);
	n = list_size(&upper_recv_list);
	_unlock(&upper_recv_lock);
	return n;
}

int get_lsq_num()
{
	int n;
	_lock(&lower_send_lock);
	n = list_size(&lower_send_list);
	_unlock(&lower_send_lock);
	return n;
}
#endif

void proc_timer(void)
{
	long long int now = ts_msec();
	int i;

	_lock(&sse_lock);
	for (i=0; i<NUM_OF_SE; ++i) {
		sse_t *se = sse[i];

		if (!se)
			continue;

		_lock(&se->lock);
		if (se->ack_timeout > 0 && now >= se->ack_timeout) {
			se->idle_timeout = ts_msec() + IDLE_TIMEOUT;

			if (se->retry > MAX_RETRY_TIMES) {
				struct list *list_node;
				send_node_t *pkt;

				list_node = list_pop_front(&se->pkt_list);
				se->is_waiting = false;
				se->ack_timeout = -1;	// remove ack-timer
				se->retry = 0;
				try_lower_send(se);
				_unlock(&se->lock);
#if __MILO_INFO_LEVEL__ >= 1
				DBG("(proc timer)exceed max retry"	\
						"-time(%d), drop\n",
						MAX_RETRY_TIMES);
#endif
				ASSIGN_CONTAINER(pkt, list_node, link);
				free(pkt);

				continue;
			}

			se->is_waiting = false;
			try_lower_send(se);
			_unlock(&se->lock);
			continue;
		}
		_unlock(&se->lock);

		if (now >= se->idle_timeout) {
			send_node_t *pkt,*next;

			sse[i] = NULL;

			_lock_destroy(&se->lock);
			LIST_FOR_EACH_SAFE (pkt, next, link, &se->pkt_list) {
				list_remove(&pkt->link);
				free(pkt);
			}
			free(se);
		}
	}
	_unlock(&sse_lock);

	_lock(&rse_lock);
	for (i=0; i<NUM_OF_SE; ++i) {
		rse_t *se = rse[i];

		if (se && now > se->idle_timeout) {
			rse[i] = NULL;

			_lock_destroy(&se->lock);
			free(se);
		}
	}
	_unlock(&rse_lock);
}

/*
 * thread safe
 * return value:
 *   -1		invalid msglen
 *  > 0		actual length buffered (to send)
 */
int upper_send(u8 dmac, u8 smac, void *msg, int msglen)
{
	send_node_t *snode = (send_node_t *)xmalloc(sizeof *snode);
	frm_hdr_t *fh = (frm_hdr_t *)snode->frm;
	msg_hdr_t *mh = (msg_hdr_t *)fh->data;

	sse_t *se;
	se_key_t key;
	int n_slices;

	// just a coarse check
	if (msglen <= 0) {
#if __MILO_INFO_LEVEL__ >= 1
		DBG("(upper send)msglen out of range(%d)\n", msglen);
#endif
		goto err_out;
	}

	key.mac = dmac;
	se = get_sse(&key);
	if (!se)
		se = create_sse(&key);

	// FIXME: normally, dmac in the first place
	fh->src = smac;
	fh->dst = dmac;
	fh->type = FRAME_TYPE_DATA;
	fh->reserved = 0;

	mh->seq = se->seq++; se->seq &= 0x01;
	n_slices = fill_slices(
			snode->frm + FRM_MSG_HDR_LEN, LINK_DATA_MAX,
			msg, &msglen);
	// fill length related part...
	// we may need cut some tail slices after escaping,
	// if it exceeds link MTU
	snode->esc_len = 0;
	do {
		int result;
		u16 len;

		mh->len = msglen;	// pure data length, not including slice hdrs
		mh->csum = 0;
		mh->csum = csum8(0, mh, sizeof *mh);
		fh->len = sizeof(slice_t)*n_slices + FRM_MSG_HDR_LEN;
		fh->csum = 0;
		fh->csum = csum8(0, fh, sizeof *fh);

		snode->n_slices = n_slices;

		len = fh->len;
		result = en_frame(snode->frm, &len, snode->escaped, LINK_MTU);
		if (result > 0)
			snode->esc_len = result;
		else {
			int cut_slice;

			cut_slice = (fh->len - len + SLICE_DATA_LEN)
				/ SLICE_DATA_LEN;
			n_slices -= cut_slice;
			if (n_slices <= 0) {
#if __MILO_INFO_LEVEL__ >= 1
				DBG("(upper send)can not fill any slice\n");
#endif
				goto err_out;
			}
			msglen = SLICE_DATA_LEN * n_slices;
		}
	} while(snode->esc_len <= 0);

	_lock(&se->lock);
	list_push_back(&se->pkt_list, &snode->link);
	_unlock(&se->lock);

	try_lower_send(se);

	return msglen;

err_out:
	free(snode);
	return -1;
}

/*
 * 'buf' has 'len' bytes of space
 * return the length dropped (for lack of buf space)
 * save to len the actual data length copied to buf
 */
int upper_recv(u8 *buf, int *len)
{
	int ret = -1;
	data_node_t *r;
	struct list *node;

	_lock(&upper_recv_lock);
	if (!list_is_empty(&upper_recv_list)) {
		node = list_pop_front(&upper_recv_list);
		_unlock(&upper_recv_lock);
		ASSIGN_CONTAINER(r, node, link);
		*len = r->len <= *len ? r->len:*len;
		memcpy(buf, r->data, *len);
		ret = r->len - *len;
		free(node);
	}
	else {
		_unlock(&upper_recv_lock);
		*len = 0;
	}

#ifdef EVALUATION
	e_valid += *len;
#endif
	return ret;
}

/*
 * 'buf' has 'len' bytes of space
 * return 0 on normal(queue is empty or data fetched)
 * return negative on error(data node always moved away from list)
 *        -1 *len is too small to hold a intact frame
 * save to len the actual data length copied to buf
 */
int lower_fetch(u8 *buf, int *len)
{
	int ret = 0;
	data_node_t *s;
	struct list *node;

	_lock(&lower_send_lock);
	if (!list_is_empty(&lower_send_list)) {
		node = list_pop_front(&lower_send_list);
		_unlock(&lower_send_lock);
		ASSIGN_CONTAINER(s, node, link);
#ifdef EVALUATION
		e_tot += s->len;
#endif
		if (s->len <= *len) {
			*len = s->len;
			memcpy(buf, s->data, s->len);
		}
		else {
			*len = 0;
			ret = -1;
		}
		free(node);
	}
	else {
		_unlock(&lower_send_lock);
		*len = 0;
	}

	return ret;
}

/*
 * when link layer get some data, it should invoke this method
 */
int lower_put(void *raw_frm, int rawlen)
{
	u8 buf[LINK_MTU];
	frm_hdr_t *fh = (frm_hdr_t *)buf;
	msg_hdr_t *mh = (msg_hdr_t *)fh->data;
	se_key_t key;
	u8 csum_save;
	u16 len;
	int ret;

#ifdef EVALUATION
	e_tot += rawlen;
#endif
	len = sizeof buf;
	ret = de_frame(raw_frm, rawlen, buf, &len);
	if (ret < 0) {
#if __MILO_INFO_LEVEL__ >= 3
		DBG("(lower put)de-frame err(%d)\n", len);
#endif
		// continue trying to get some useful data
	}
	if (len < sizeof *fh) {
#if __MILO_INFO_LEVEL__ >= 3
		DBG("(lower put)frame's too short(%d)\n", len);
#endif
		goto err_out;
	}
	csum_save = fh->csum;
	fh->csum = 0;
	if (csum_save != csum8(0, fh, sizeof *fh)) {
#if __MILO_INFO_LEVEL__ >= 3
		DBG("(lower put)frame csum err\n");
#endif
		goto err_out;
	}

	switch (fh->type) {
		case FRAME_TYPE_DATA:
			proc_data(buf, len);
			break;
		case FRAME_TYPE_ACKR:
			proc_ack_req(buf, len);
			break;
		case FRAME_TYPE_PATCH:
			proc_patch(buf, len);
			break;
		case FRAME_TYPE_ACKA:
			proc_ack_all(buf, len);
			break;
		case FRAME_TYPE_ACKP:
			proc_ack_part(buf, len);
			break;
		default:
#if __MILO_INFO_LEVEL__ >= 1
			DBG("(lower put)unknown type->%d\n", fh->type);
#endif
			goto err_out;
	}
	return rawlen;

err_out:
	return -1;
}

void split_init()
{
	int i;

	_lock_init(&upper_recv_lock);
	list_init(&upper_recv_list);

	_lock_init(&lower_send_lock);
	list_init(&lower_send_list);

	for (i=0; i<NUM_OF_SE; ++i) {
		sse[i] = NULL;
		rse[i] = NULL;
	}
}

void split_cleanup()
{
	data_node_t *data_node, *next;

	LIST_FOR_EACH_SAFE (data_node, next, link, &upper_recv_list) {
		list_remove(&data_node->link);
		free(data_node);
	}
	_lock_destroy(&upper_recv_lock);

	LIST_FOR_EACH_SAFE (data_node, next, link, &lower_send_list) {
		list_remove(&data_node->link);
		free(data_node);
	}
	_lock_destroy(&lower_send_lock);
}
