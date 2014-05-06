sse_t sse[256];	// send sessions, 8-bit key
rse_t rse[256];	// recv sessions, 8-bit key

/*
 * return value:
 *   -1		frm size's too small
 * >= 0		actual length in buf
 */
int en_frame(frm_hdr_t *hdr, u8 *data, u16 data_len, u8 *frm, u16 size)
{
	int len;
	int part;
	u8 *ch;
	u8 *p;

	p = frm;
	len = -1;

	if (size < 1)
		goto err_out;
	*p = BREAK_CHAR;

	part = 0;
	while (part < 2) {
		u16 i, part_size;

		switch (part) {
			case 0:
				ch = (u8 *)hdr;
				part_size = sizeof *hdr;
				break;
			case 1:
				ch = (u8 *)data;
				part_size = data_len;
				break;
			default:
				goto err_out;
		}

		for (i=0; i<part_size; ++i) {
			switch (*ch) {
				case BREAK_CHAR:
					if (p - frm + 2 > size)
						goto err_out;
					*(++p) = ESCAPE_CHAR;
					*(++p) = ESCAPE_BREAK;
					break;
				case ESCAPE_CHAR:
					if (p - frm + 2 > size)
						goto err_out;
					*(++p) = ESCAPE_CHAR;
					*(++p) = ESCAPE_ESCAPE;
					break;
				default:
					if (p - frm + 1 > size)
						goto err_out;
					*(++p) = *(ch++);
			}
		}

		++ part;
	}

	if (p - frm + 1 > size)
		goto err_out;
	*(++p) = BREAK_CHAR;
	len = p - frm;

err_out:
	return len;
}

/*
 * return value:
 *   -1		frm/buf size's too small
 *   -2		escape followed by nothing
 *   -3		unknown escaped char
 * >= 0		actual length in buf
 */
int de_frame(u8 *frm, u16 frm_len, u8 *buf, u16 size)
{
	int len;
	int part;
	u8 *ch;
	u8 *p;
	int find_start_break;

	p = buf;
	len = -1;

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
					len = p - buf;
					goto out;	// success
				}
				break;
			case ESCAPE_CHAR:
				if (ch-frm + 1 > frm_len) {
					len = -2;
					goto out;	// err: end-up by
							//      escape
				}
				switch (*(++ch)) {
					case ESCAPE_BREAK:
						if (p-buf + 1 > size)
							// err: no more root
							//      to store data
							goto out;
						*(p++) = BREAK_CHAR;
						break;
					case ESCAPE_ESCAPE:
						if (p-buf + 1 > size)
							// err: no more root
							//      to store data
							goto out;
						*(p++) = ESCAPE_CHAR;
						break;
					default:
						// err: unknown escaped char
						len = -3;
						goto out;
				}
			default:
				if (p-buf + 1 > size)
					// err: no more root
					//      to store data
					goto out;
				*(p++) = *ch;
		}

		// parse next char
		++ch;
	}
	len = -1;

out:
	return len;
}

static int fill_slice_k(u8 k, void *msg, int msglen, void *dest)
{
	slice_t *slice = (slice_t *)dest;
	u8 len;

	slice->sep = SLICE_SEPERATOR;
	slice->seq = k;
	len = SLICE_DATA_LEN * (k+1) < msglen ?
		SLICE_DATA_LEN : (msglen - SLICE_DATA_LEN * seq);
	memcpy(slice->data, msg + SLICE_DATA_LEN*k, len);
	if (len < SLICE_DATA_LEN)
		bzero(slice->data + len, SLICE_DATA_LEN - len);
	slice->csum = 0;
	slice->csum = csum16(slice, sizeof *slice);
}

static int fill_slices(void *data, int size, void *msg, int msglen)
{
	int n_slices = -1;
	slice_t *slice;
	int seq;

	for (
		slice = (slice_t *)data, seq=0;
		SLICE_DATA_LEN*seq < msglen && ((void*)slice < data + size);
		++slice, ++seq 
	    ) {
		fill_slice_k(seq, msg, msglen, slice);
	}
	if (SLICE_DATA_LEN*seq >= msglen)
		n_slices = seq + 1;

	return n_slices;
}

void set_bitmap(u32 *map, u8 k)
{
	u8 offset = k>>5;	// div 32
	i = k & 0x1f;		// mod 32
	map[offset] |= 1L << i;
}

void clr_bitmap(u32 *map, u8 k)
{
	u8 offset = k>>5;	// div 32
	i = k & 0x1f;		// mod 32
	map[offset] &= ~(1L << i);
}

// find first zero pos start from k, but pos <= max
int find_first_zero(u32 *map, u8 k, u8 max)
{
	u8 start = k>>5;
	u8 end = max>>5;
	u8 bit = k & 0x1f;
	u8 x, cnt;
	cnt = 0;
	x=start;
	while (x <= end) {
		if (!(map[x] & (1L<<bit)))
			return k + cnt;
		else {
			++cnt;
			if (++bit == 32) {
				bit = 0;
				++x;
			}
		}
	}
	return -1;
}

u8 csum8(void *d, int len)
{
	return 0;
}

session_t *get_sse(key_t *k)
{
	return sse[k->mac];
}

session_t *create_sse(key_t *k, send_node_t *n)
{
	sse_t *se = (sse_t *)xmalloc(sizeof *se);

	se->is_waiting = false;
	list_init(&se->pkt_list);
	list_push_back(&se->pkt_list, &n->link);
}

session_t *remove_sse(key_t *k)
{
	sse_t *se = get_sse(k);

	if (se) {
		sse[k->mac] = NULL;

		LIST_FOR_EACH_SAFE (node, next, link, &se->pkt_list) {
			list_remove(&node->link);
		}
		free se;
	}
}

session_t *get_rse(key_t *k)
{
	return rse[k->mac];
}

session_t *create_rse(key_t *k)
{
	rse_t *se = (rse_t *)xmalloc(sizeof *se);
	int i;

	for (i=0; i<sizeof se->bitmap; ++i)
		se->bitmap[i] = 0;
	se->n_slices = 0;
}

session_t *remove_rse(key_t *k)
{
	rse_t *se = get_rse(k);

	if (se) {
		rse[k->mac] = NULL;

		free(se);
	}
}

void try_lower_send(session_t *se)
{
	send_node_t *pkt;
	struct list *node;

	if (se->is_waiting)
		list_push_back(pkt_list, &pkt->node);
	else {
		node = list_front(se->pkt_list);
		ASSIGN_CONTAINER(pkt, node, link);
		lower_send(pkt->escaped, pkt->esc_len);
	}
}

int upper_send(u8 smac, u8 dmac, void *msg, int msglen)
{
	send_node_t *node = (send_node_t *)xmalloc(sizeof *node);
	frm_hdr_t *fh = (frm_hdr_t *)node->frm;
	msg_hdr_t *mh = (msg_hdr_t *)(node->frm + sizeof *fh);
	session_t *se;
	key_t key;
	int ret = -1;
	int n_slices;

	if (msglen > LINK_MTU - sizeof *fh)
		goto err_out;

	fh->src = smac;
	fh->dst = dmac;
	fh->type = FRAME_TYPE_DATA;
	fh->reserved = 0;
	fh->csum = 0;
	mh->slice.len = msglen;
	n_slices = fill_slices(
			node->frm + FRM_MSG_HDR_LEN, LINK_DATA_MAX,
			msg, msglen);
	if (n_slices = < 0)
		goto err_out;
	fh->len = SLICE_DATA_LEN*n_slices + FRM_MSG_HDR_LEN;
	fh->csum = csum8(fh, sizeof *fh);

	node->esc_len = en_frame(node->escaped, LINK_MTU, tmp, fh->len);
	if (node->esc_len < 0)
		goto err_out;

	key = {dmac};
	se = get_session(&key);
	if (!se)
		se = create_sse(&key, node);

	try_lower_send(se);

	return msglen;

err_out:
	free node;
	return ret;
}

int upper_recv()
{
	if (recv_queue is not empty)
		return data;
	else
		return null;
}

int lower_send()
{
	return 0;
}

int lower_recv(void *raw, int len)
{
	de_frame();

	if (type == MESSAGE_DATA || type == MESSAGE_REQ_ACK) {
		if (type == MESSAGE_DATA) {
			if (is new session)
				create new recv_session;
			else
				find recv_session;
			if (message complete)
				insert into recv queue;
		}
		else if (type == MESSAGE_REQ_ACK) {
			find recv_session;
			build ack message & lower_send;
		}
		else {
			// can't reach here
			error;
		}
	}
	else if (type == MESSAGE_ACK_ALL || type == MESSAGE_ACK_PAR) {
		find send_session;

		if (type == MESSAGE_ACK_ALL) {
			complete session;
			if (send_queue not empty)
				lower_send();
		}
		if (type == MESSAGE_ACK_PAR) {
			build partly message & lower_send;
		}
	}
	else {
		// unknow type
		error;
	}
}

proc_timer()
{
	for_each_in_send_queue {
		if (ack timeout) {
			if (excceed max retry time)
				remove;
			else
				request_ack;
		}
		else if (idle_timeout)
			remove;
	}
	for_each_in_recv_queue {
		if (idel timeout) {
			remove;
		}
	}
}

