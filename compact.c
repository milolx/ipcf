#include <string.h>
#include <assert.h>
#include <arpa/inet.h>

#include "types.h"
#include "list.h"
#include "hmap.h"
#include "compact.h"
#include "idpool.h"
#include "ts.h"
#include "hash.h"
#include "csum.h"
#include "atomic.h"

typedef struct {
	struct hmap_node node;
#define STATE_NEW		1
#define STATE_PENDING		2
#define STATE_ESTABLISHED	3
	u8 state;
	skey_t skey;
	struct orig_hdr hdr;
	u8 fid;		// compact-ipid, also used as index
	int soft_timeout;
}sflow_node_t;

typedef struct {
	struct hmap_node node;
	rkey_t rkey;
	struct orig_hdr hdr;
	u8 fid;		// compact-ipid, also used as index
	int soft_timeout;
}rflow_node_t;

typedef struct {
	struct hmap_node node;
	u8 id;
	u16 ipid;
	sflow_node_t *flow;
}frag_node_t;

static struct hmap sflow_hmap;
static struct hmap rflow_hmap;
//static struct hmap frag_hmap;

light_lock_t sflow_lock;
light_lock_t rflow_lock;

idpool_t *idp;
static sflow_node_t* sflow_rindex[1<<N_ID_BITS];


#if 0
static frag_node_t* locate_in_frag_hmap(u16 ipid)
{
	frag_node_t *n;
	u32 hval;

	hval = hash_bytes(&ipid, sizeof ipid);
	HMAP_FOR_EACH_WITH_HASH(n, node, hval, &frag_hmap) {
		if (n->ipid == ipid)
			return n;
	}
	return NULL;
}
#endif

static skey_t get_sflow_key(void *ippkt)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;
	struct tcphdr *tcphdr = (struct tcphdr *)(ippkt + sizeof *iphdr);
	struct udphdr *udphdr = (struct udphdr *)(ippkt + sizeof *iphdr);

	static skey_t k;

	k.resv = 0;

	k.saddr = iphdr->saddr;
	k.daddr = iphdr->daddr;
	k.proto = iphdr->protocol;
	if ((ntohs(iphdr->frag_off) & FRAG_FLAG_MF_MASK)
			|| (ntohs(iphdr->frag_off) & FRAG_OFF_MASK)
			|| ((iphdr->protocol != PROTOCOL_TCP)
				&& (iphdr->protocol != PROTOCOL_UDP))) {
		k.sport = 0;
		k.dport = 0;
		k.ipid = iphdr->id;
	}
	else if (iphdr->protocol == PROTOCOL_TCP) {
		k.ipid = 0;
		k.sport = tcphdr->source;
		k.dport = tcphdr->dest;
	}
	else if (iphdr->protocol == PROTOCOL_UDP) {
		k.ipid = 0;
		k.sport = udphdr->source;
		k.dport = udphdr->dest;
	}
	else {
		assert(1);	// never happen
	}

	return k;
}

static sflow_node_t* get_sflow_by_hash(u32 hval, skey_t *skey)
{
	sflow_node_t *n;

	_lock(&sflow_lock);
	HMAP_FOR_EACH_WITH_HASH(n, node, hval, &sflow_hmap) {
		if (!memcmp(&n->skey, skey, sizeof *skey)) {
			_unlock(&sflow_lock);
			return n;
		}
	}
	_unlock(&sflow_lock);
	return NULL;
}

static sflow_node_t* build_sflow_by_hash(u32 hval, skey_t *skey, void *ippkt, u8 fid)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;
	struct tcphdr *tcphdr = (struct tcphdr *)(ippkt + sizeof *iphdr);
	struct udphdr *udphdr = (struct udphdr *)(ippkt + sizeof *iphdr);

	sflow_node_t *n;

	n = xmalloc(sizeof *n);
	n->state = STATE_NEW;
	n->skey = *skey;
	n->hdr.ip = *iphdr;
	if (iphdr->protocol == PROTOCOL_TCP)
		n->hdr.t.tcp = *tcphdr;
	else if (iphdr->protocol == PROTOCOL_UDP)
		n->hdr.t.udp = *udphdr;

	n->fid = fid;
	n->soft_timeout = ts_msec() + SOFT_TIMEOUT_INTERVAL;
	_lock(&sflow_lock);
	hmap_insert(&sflow_hmap, &n->node, hval);
	_unlock(&sflow_lock);

	return n;
}

static void touch_sflow(sflow_node_t* flow)
{
	flow->soft_timeout = ts_msec() + SOFT_TIMEOUT_INTERVAL;
}

static void touch_rflow(rflow_node_t* flow)
{
	flow->soft_timeout = ts_msec() + SOFT_TIMEOUT_INTERVAL;
}

static void add_to_pkt_list(struct list *pkt_list, void *data, u16 len)
{
	pkt_t *pkt;

	pkt = xmalloc(sizeof *pkt);
	pkt->len = len;
	memcpy(pkt->data, data, len);
	list_push_back(pkt_list, &pkt->node);
}

static void build_ctl(struct list *pkt_list, sflow_node_t *sflow)
{
	pkt_t *pkt;
	chdr_t *ctl_hdr;

	pkt = xmalloc(sizeof *pkt);
	ctl_hdr = (chdr_t *)pkt->data;
	ctl_hdr->t = CTYPE_FRM_CTL;
	ctl_hdr->i = CTYPE_CTL_REQ;
	ctl_hdr->smac = MYMAC;
	ctl_hdr->id = sflow->fid;
	pkt->len = sizeof *ctl_hdr;
	memcpy(pkt->data + pkt->len, &sflow->hdr.ip, sizeof sflow->hdr.ip);
	pkt->len += sizeof sflow->hdr.ip;
	if (sflow->hdr.ip.protocol == PROTOCOL_TCP) {
		memcpy(pkt->data + pkt->len, &sflow->hdr.t.tcp, sizeof sflow->hdr.t.tcp);
		pkt->len += sizeof sflow->hdr.t.tcp;
	}
	else if (sflow->hdr.ip.protocol == PROTOCOL_UDP) {
		memcpy(pkt->data + pkt->len, &sflow->hdr.t.udp, sizeof sflow->hdr.t.udp);
		pkt->len += sizeof sflow->hdr.t.udp;
	}

	list_push_back(pkt_list, &pkt->node);
}

static void build_cpkt(struct list *pkt_list, sflow_node_t *sflow, void *ippkt)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;
	struct tcphdr *tcphdr = (struct tcphdr *)(ippkt + sizeof *iphdr);
	struct udphdr *udphdr = (struct udphdr *)(ippkt + sizeof *iphdr);

	pkt_t *pkt;
	chdr_t *chdr;

	pkt = xmalloc(sizeof *pkt);
	chdr = (chdr_t *)pkt->data;
	chdr->i = 0;
	chdr->smac = MYMAC;
	chdr->id = sflow->fid;
	pkt->len = sizeof *chdr;

	if (sflow->skey.ipid) {
		chdr->t = CTYPE_FRM_RAW;

		if (ntohs(iphdr->frag_off) & FRAG_FLAG_MF_MASK)
			chdr->i |= CTYPE_RAW_MFBIT;
		if (ntohs(iphdr->frag_off) & FRAG_OFF_MASK) {
			u16 frag_off;

			chdr->i |= CTYPE_RAW_FBIT;

			frag_off = ntohs(iphdr->frag_off) & FRAG_OFF_MASK;
			frag_off = htons(frag_off);
			memcpy(pkt->data + pkt->len, &frag_off, sizeof frag_off);
			pkt->len += sizeof frag_off;
		}

		memcpy(pkt->data + pkt->len,
				ippkt + sizeof *iphdr,
				ntohs(iphdr->tot_len) - sizeof *iphdr);
		pkt->len += ntohs(iphdr->tot_len) - sizeof *iphdr;

		list_push_back(pkt_list, &pkt->node);
	}
	else if (iphdr->protocol == PROTOCOL_TCP) {
		chdr->t = CTYPE_FRM_TCP;

		if (tcphdr->urg) {
			if (tcphdr->syn)
				chdr->i |= CTYPE_TCP_URGSYN;
			else if (tcphdr->fin)
				chdr->i |= CTYPE_TCP_URGFIN;
			else if (tcphdr->psh)
				chdr->i |= CTYPE_TCP_URGPSH;
			else
				chdr->i |= CTYPE_TCP_URG;
		}
		else {
			if (tcphdr->syn)
				chdr->i |= CTYPE_TCP_SYN;
			else if (tcphdr->fin)
				chdr->i |= CTYPE_TCP_FIN;
			else if (tcphdr->psh)
				chdr->i |= CTYPE_TCP_PSH;
			else if (tcphdr->rst)
				chdr->i |= CTYPE_TCP_RST;
			else
				assert(1);
		}

		chdr->i |= CTYPE_TCP_SBIT;
		memcpy(pkt->data + pkt->len, &tcphdr->seq, sizeof tcphdr->seq);
		pkt->len += sizeof tcphdr->seq;

		if (tcphdr->ack) {
			chdr->i |= CTYPE_TCP_ABIT;
			memcpy(pkt->data + pkt->len, &tcphdr->ack_seq, sizeof tcphdr->ack_seq);
			pkt->len += sizeof tcphdr->ack_seq;
		}

		chdr->i |= CTYPE_TCP_WBIT;
		memcpy(pkt->data + pkt->len, &tcphdr->window, sizeof tcphdr->window);
		pkt->len += sizeof tcphdr->window;

		if (tcphdr->urg) {
			memcpy(pkt->data + pkt->len, &tcphdr->urg_ptr, sizeof tcphdr->urg_ptr);
			pkt->len += sizeof tcphdr->urg_ptr;
		}

		memcpy(pkt->data + pkt->len, &tcphdr->check, sizeof tcphdr->check);
		pkt->len += sizeof tcphdr->check;

		memcpy(pkt->data + pkt->len,
				ippkt + sizeof *iphdr + sizeof *tcphdr,
				ntohs(iphdr->tot_len) - sizeof *iphdr
				- sizeof *tcphdr);
		pkt->len += ntohs(iphdr->tot_len) - sizeof *iphdr - sizeof *tcphdr;
	}
	else if (iphdr->protocol == PROTOCOL_UDP) {
		chdr->t = CTYPE_FRM_UDP;

		memcpy(pkt->data + pkt->len, &udphdr->check, sizeof udphdr->check);
		pkt->len += sizeof udphdr->check;

		memcpy(pkt->data + pkt->len,
				ippkt + sizeof *iphdr + sizeof *udphdr,
				ntohs(iphdr->tot_len) - sizeof *iphdr
				- sizeof *udphdr);
		pkt->len += ntohs(iphdr->tot_len) - sizeof *iphdr
				- sizeof *udphdr;
	}
	else {
		assert(1);	// never happen
	}

	list_push_back(pkt_list, &pkt->node);
}

static rflow_node_t* get_rflow_by_hash(u32 hval, rkey_t *rkey)
{
	rflow_node_t *n;

	_lock(&rflow_lock);
	HMAP_FOR_EACH_WITH_HASH(n, node, hval, &rflow_hmap) {
		if (!memcmp(&n->rkey, rkey, sizeof *rkey)) {
			_unlock(&rflow_lock);
			return n;
		}
	}
	_unlock(&rflow_lock);
	return NULL;
}

static pkt_t* recv_untouched_ip(void *ippkt)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;
	pkt_t *pkt;
	u16 len;

	pkt = xmalloc(sizeof *pkt);
	len = ntohs(iphdr->tot_len) <= MAX_DATA_LENGTH ? ntohs(iphdr->tot_len):MAX_DATA_LENGTH;
	memcpy(pkt->data, ippkt, len);
	pkt->len = len;

	return pkt;
}

static pkt_t* build_ack(void *cpkt)
{
	chdr_t *chdr = (chdr_t *)cpkt;
	pkt_t *pkt;
	chdr_t *ctl_hdr;

	pkt = xmalloc(sizeof *pkt);
	ctl_hdr = (chdr_t *)pkt->data;
	ctl_hdr->t = CTYPE_FRM_CTL;
	ctl_hdr->i = CTYPE_CTL_ACK;
	ctl_hdr->smac = chdr->smac;
	ctl_hdr->id = chdr->id;
	pkt->len = sizeof *ctl_hdr;

	return pkt;
}

static rflow_node_t* build_rflow_by_hash(uint32_t hval, rkey_t *rkey, void *cpkt)
{
	chdr_t *chdr = (chdr_t *)cpkt;
	struct iphdr *iphdr = (struct iphdr *)(cpkt + sizeof *chdr);
	struct tcphdr *tcphdr = (struct tcphdr *)(cpkt + sizeof *chdr + sizeof *iphdr);
	struct udphdr *udphdr = (struct udphdr *)(cpkt + sizeof *chdr + sizeof *iphdr);

	rflow_node_t *n;

	n = xmalloc(sizeof *n);
	n->rkey = *rkey;
	n->hdr.ip = *iphdr;
	if (iphdr->protocol == PROTOCOL_TCP)
		n->hdr.t.tcp = *tcphdr;
	else if (iphdr->protocol == PROTOCOL_UDP)
		n->hdr.t.udp = *udphdr;
	else {
		assert(1);	// never happen
	}
	n->fid = chdr->id;
	n->soft_timeout = ts_msec() + SOFT_TIMEOUT_INTERVAL;
	_lock(&rflow_lock);
	hmap_insert(&rflow_hmap, &n->node, hval);
	_unlock(&rflow_lock);

	return n;
}

static rkey_t get_rflow_key(chdr_t *chdr)
{
	static rkey_t k;

	k.smac = chdr->smac;
	k.id = chdr->id;

	return k;
}

static pkt_t* recv_tcp(void *cpkt, u16 len, rflow_node_t *rflow)
{
	chdr_t *chdr = (chdr_t *)cpkt;
	pkt_t *pkt;
	struct iphdr *iphdr;
	struct tcphdr *tcphdr;
	int offset;

	// make it diff... in fact, any num is fine
	rflow->hdr.ip.id = htons(ntohs(rflow->hdr.ip.id) + 1);

	pkt = xmalloc(sizeof *pkt);
	iphdr = (struct iphdr *)pkt->data;
	memcpy(iphdr, &rflow->hdr.ip, sizeof *iphdr);
	tcphdr = (struct tcphdr *)(pkt->data + sizeof *iphdr);
	memcpy(tcphdr, &rflow->hdr.t, sizeof *tcphdr);

	pkt->len = sizeof *iphdr + sizeof *tcphdr;

	offset = sizeof *chdr;

	if (chdr->i & CTYPE_TCP_SBIT) {
		memcpy(&tcphdr->seq, cpkt + offset, sizeof tcphdr->seq);
		offset += sizeof tcphdr->seq;
	}

	if (chdr->i & CTYPE_TCP_ABIT) {
		tcphdr->ack = 1;
		memcpy(&tcphdr->ack_seq, cpkt + offset, sizeof tcphdr->ack_seq);
		offset += sizeof tcphdr->ack_seq;
	}
	else
		tcphdr->ack = 0;

	if (chdr->i & CTYPE_TCP_WBIT) {
		memcpy(&tcphdr->window, cpkt + offset, sizeof tcphdr->window);
		offset += sizeof tcphdr->window;
	}

	if (!(chdr->i & CTYPE_TCP_SBMASK)
			|| ((chdr->i & CTYPE_TCP_SBMASK) >= CTYPE_TCP_URGSYN)) {
		tcphdr->urg = 1;
		memcpy(&tcphdr->urg_ptr, cpkt + offset, sizeof tcphdr->urg_ptr);
		offset += sizeof tcphdr->urg_ptr;
	}
	else
		tcphdr->urg = 0;

	memcpy(&tcphdr->check, cpkt + offset, sizeof tcphdr->check);
	offset += sizeof tcphdr->check;

	memcpy(pkt->data + sizeof *iphdr + sizeof *tcphdr,
			cpkt + offset,
			len - offset);
	pkt->len += len - offset;

	tcphdr->syn = tcphdr->fin = tcphdr->psh = tcphdr->rst = 0;
	switch (chdr->i & CTYPE_TCP_SBMASK) {
		case CTYPE_TCP_SYN:
		case CTYPE_TCP_URGSYN:
			tcphdr->syn = 1;
			break;
		case CTYPE_TCP_FIN:
		case CTYPE_TCP_URGFIN:
			tcphdr->fin = 1;
			break;
		case CTYPE_TCP_PSH:
		case CTYPE_TCP_URGPSH:
			tcphdr->psh = 1;
			break;
		case CTYPE_TCP_RST:
			tcphdr->rst = 1;
			break;
		case CTYPE_TCP_URG:
			// do nothing(we have set)
			break;
		default:
			break;
	}

	iphdr->tot_len = htons(pkt->len);
	iphdr->check = 0;
	iphdr->check = csum(iphdr, sizeof *iphdr);

	return pkt;
}

static pkt_t* recv_udp(void *cpkt, u16 len, rflow_node_t *rflow)
{
	chdr_t *chdr = (chdr_t *)cpkt;
	pkt_t *pkt;
	struct iphdr *iphdr;
	struct udphdr *udphdr;
	int offset;

	// make it diff... in fact, any num is fine
	rflow->hdr.ip.id = htons(ntohs(rflow->hdr.ip.id) + 1);

	pkt = xmalloc(sizeof *pkt);
	iphdr = (struct iphdr *)pkt->data;
	memcpy(iphdr, &rflow->hdr.ip, sizeof *iphdr);
	udphdr = (struct udphdr *)(pkt->data + sizeof *iphdr);
	memcpy(udphdr, &rflow->hdr.t, sizeof *udphdr);

	pkt->len = sizeof *iphdr + sizeof *udphdr;

	offset = sizeof *chdr;

	memcpy(&udphdr->check, cpkt + offset, sizeof udphdr->check);
	offset += sizeof udphdr->check;

	udphdr->len = htons(len - offset + sizeof *udphdr);

	memcpy(pkt->data + sizeof *iphdr + sizeof *udphdr,
			cpkt + offset,
			len - offset);
	pkt->len += len - offset;

	iphdr->tot_len = htons(pkt->len);
	iphdr->check = 0;
	iphdr->check = csum(iphdr, sizeof *iphdr);

	return pkt;
}

static pkt_t* recv_raw(void *cpkt, u16 len, rflow_node_t *rflow)
{
	chdr_t *chdr = (chdr_t *)cpkt;
	pkt_t *pkt;
	struct iphdr *iphdr;
	int offset;

	// use the exact same ip-id

	pkt = xmalloc(sizeof *pkt);
	iphdr = (struct iphdr *)pkt->data;
	memcpy(iphdr, &rflow->hdr.ip, sizeof *iphdr);

	pkt->len = sizeof *iphdr;

	offset = sizeof *chdr;

	iphdr->frag_off = 0;
	if (chdr->i & CTYPE_RAW_FBIT) {
		memcpy(&iphdr->frag_off, cpkt + offset, sizeof iphdr->frag_off);
		iphdr->frag_off = ntohs(iphdr->frag_off);
		iphdr->frag_off &= FRAG_OFF_MASK;
		iphdr->frag_off = htons(iphdr->frag_off);
		offset += sizeof iphdr->frag_off;
	}
	if (chdr->i & CTYPE_RAW_MFBIT) {
		iphdr->frag_off = ntohs(iphdr->frag_off);
		iphdr->frag_off |= FRAG_FLAG_MF_MASK;
		iphdr->frag_off = htons(iphdr->frag_off);
	}

	memcpy(pkt->data + sizeof *iphdr, cpkt + offset, len - offset);
	pkt->len += len - offset;

	iphdr->tot_len = htons(pkt->len);
	iphdr->check = 0;
	iphdr->check = csum(iphdr, sizeof *iphdr);

	return pkt;
}

void timer_event()
{
	sflow_node_t *s, *sn;
	rflow_node_t *r, *rn;
	long long int now = ts_msec();

	_lock(&sflow_lock);
	HMAP_FOR_EACH_SAFE(s, sn, node, &sflow_hmap) {
		if (now < s->soft_timeout)
			continue;

		hmap_remove(&sflow_hmap, &s->node);
		sflow_rindex[s->fid] = NULL;
		release_id(idp, s->fid);
		free(s);
		// timeout
		/*
		switch (flow->state) {
			case STATE_NEW:
				break;
			case STATE_PENDING:
				if (timout1)
					remove_flow();
				break;
			case STATE_ESTABLISHED:
				if (timout2)
					remove_flow();
				break;
			default:
				break;
		}
		*/
	}
	_unlock(&sflow_lock);

	_lock(&rflow_lock);
	HMAP_FOR_EACH_SAFE(r, rn, node, &rflow_hmap) {
		if (now < r->soft_timeout)
			continue;

		hmap_remove(&rflow_hmap, &r->node);
		free(r);
	}
	_unlock(&rflow_lock);
}

void compact_init()
{
	hmap_init(&sflow_hmap);
	hmap_init(&rflow_hmap);

	_lock_init(&sflow_lock);
	_lock_init(&rflow_lock);

	idp = init_idpool(1<<N_ID_BITS);
}

void xmit_compress(void *ippkt, struct list *pkt_list)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;
	struct tcphdr *tcphdr = (struct tcphdr *)(ippkt + sizeof *iphdr);

	skey_t skey;
	u32 hval;
	sflow_node_t *sflow;

	list_init(pkt_list);

	if (iphdr->version != 4 || iphdr->ihl != 5
		|| (iphdr->protocol == PROTOCOL_TCP && tcphdr->doff != 5)) {
		// the pkt is not ipv4 or has ip options
		// or has tcp options
		goto can_not_compact;
	}
	if (csum(iphdr, sizeof *iphdr)) {
		// checksum error or even not a ip pkt
		goto can_not_compact;
	}

	sflow = NULL;

	// the actrual key for hash is frag-or-not related
	skey = get_sflow_key(ippkt);
	hval = hash_bytes(&skey, sizeof skey);

	sflow = get_sflow_by_hash(hval, &skey);

	if (!sflow) {	// new flow
		int tmp;
		u8 fid;

		tmp = get_id(idp);
		if (tmp < 0) {
			printf("err: get_id = %d\n", tmp);
			goto err_out;
		}
		else
			fid = (u8) tmp;
		// reverse-locate flow entry also via fid

		sflow = build_sflow_by_hash(hval, &skey, ippkt, fid);
		sflow_rindex[fid] = sflow;
	}

	assert(sflow);

	switch (sflow->state) {
		case STATE_NEW:
			build_ctl(pkt_list, sflow);
			add_to_pkt_list(pkt_list, ippkt, ntohs(iphdr->tot_len));
			sflow->state = STATE_PENDING;
			break;
		case STATE_PENDING:
			add_to_pkt_list(pkt_list, ippkt, ntohs(iphdr->tot_len));
			break;
		case STATE_ESTABLISHED:
			build_cpkt(pkt_list, sflow, ippkt);
			break;
		default:
			assert(1);	// never happen
			break;
	}

	touch_sflow(sflow);
	return;

can_not_compact:
	add_to_pkt_list(pkt_list, ippkt, ntohs(iphdr->tot_len));
err_out:
	return;
}

// return 0 on success
int recv_compress(void *cpkt, int len, pkt_t **ippkt, pkt_t **send_back_pkt)
{
	struct iphdr *iphdr = (struct iphdr *)cpkt;
	chdr_t *chdr = (chdr_t *)cpkt;

	rflow_node_t *rflow;
	rkey_t rkey;
	uint32_t hval;

	*ippkt = NULL;
	*send_back_pkt = NULL;

	if (iphdr->version == 4 && iphdr->ihl >= 5) {
		if (!csum(iphdr, sizeof *iphdr)) {
			// ipv4
			*ippkt = recv_untouched_ip(cpkt);
			return 0;
		}
	}

	rkey = get_rflow_key(chdr);
	hval = hash_bytes(&rkey, sizeof rkey);
	rflow = get_rflow_by_hash(hval, &rkey);

	switch (chdr->t) {
		case CTYPE_FRM_CTL:
			switch (chdr->i) {
				case CTYPE_CTL_REQ:
					assert(!rflow);
					rflow = build_rflow_by_hash(hval, &rkey, cpkt);

					*send_back_pkt = build_ack(cpkt);
					break;
				case CTYPE_CTL_ACK:
					if (sflow_rindex[chdr->id])
						sflow_rindex[chdr->id]->state = STATE_ESTABLISHED;
					else
						printf("sflow_rindex[%d] is null\n", chdr->id);
					break;
				case CTYPE_CTL_FLT:
					break;
				default:
					assert(0);
					break;
			}
			break;
		case CTYPE_FRM_TCP:
			assert(rflow);
			touch_rflow(rflow);
			*ippkt = recv_tcp(cpkt, len, rflow);
			break;
		case CTYPE_FRM_UDP:
			assert(rflow);
			touch_rflow(rflow);
			*ippkt = recv_udp(cpkt, len, rflow);
			break;
		case CTYPE_FRM_RAW:
			assert(rflow);
			touch_rflow(rflow);
			*ippkt = recv_raw(cpkt, len, rflow);
			break;
		default:
			assert(0);
	}

	return 0;
}

