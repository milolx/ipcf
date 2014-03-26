
#define MAX_DATA_LENGTH		2000
#define SOFT_TIMEOUT_INTERVAL	(10*1000)	// in msec

typedef struct {
	struct list node;
	u16 len;
	u8 data[MAX_DATA_LENGTH];
}pkt_t;

typedef struct {
	struct hmap_node node;
#define STATE_NEW		1
#define STATE_PENDING		1
#define STATE_ESTABLISHED	1
	u8 state;
	struct key key;
	struct iphdr ip;
	union {
		struct tcphdr tcp;
		struct udphdr udp;
	}tl;
	u8 fid;		// compact-ipid, also used as index
	u8 frag_id;	// for possible frag pkts
	int soft_timeout;
}flow_node_t;

typedef struct {
	struct hmap_node node;
	u8 id;
	u16 ipid;
	flow_node_t *flow;
}frag_node_t;

struct hmap flow_hmap;
struct hmap frag_hmap;

idpool_t *idp;
static flow_node_t* flow_rindex[1<<N_ID_BITS];


void compact_init()
{
	hmap_init(&flow_hmap);
	hmap_init(&frag_hmap);

	idp = init_idpool(1<<N_ID_BITS);
}

frag_node_t* locate_in_frag_hmap(u16 ipid)
{
	frag_node_t *n;
	uint32_t hval;

	hval = hash_bytes(&ipid, sizeof ipid);
	HMAP_FOR_EACH_WITH_HASH(n, node, hval, &frag_hmap) {
		if (n->ipid == ipid)
			return n;
	}
	return NULL;
}

struct key get_flow_key(void *ippkt)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;
	struct iphdr *tcphdr = (struct tcphdr *)(ippkt + sizeof struct iphdr);
	struct iphdr *udphdr = (struct udphdr *)(ippkt + sizeof struct iphdr);

	static struct key k;

	k.saddr = iphdr->saddr;
	k.daddr = iphdr->daddr;
	k.prot = iphdr->protocol;
	if (iphdr->protocol == PROTOCOL_TCP) {
		k.sport = tcphdr->sport;
		k.dport = tcphdr->dport;
	}
	else if (iphdr->protocol == PROTOCOL_UDP) {
		k.sport = udphdr->sport;
		k.dport = udphdr->dport;
	}
	else {
		k.sport = 0;
		k.dport = 0;
	}

	return k;
}

flow_node_t* get_flow_by_hash(u32 flow_hval, struct key key)
{
	flow_node_t *n;

	HMAP_FOR_EACH_WITH_HASH(n, node, flow_hval, &flow_hmap) {
		if (n->key == key)
			return n;
	}
	return NULL;
}

flow_node_t* build_flow_by_hash(uint32_t hval, struct key *key, void *ippkt, u8 fid)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;
	struct tcphdr *tcphdr = (struct tcphdr *)(ippkt + sizeof struct iphdr);
	struct udphdr *udphdr = (struct udphdr *)(ippkt + sizeof struct iphdr);

	flow_node_t *n;

	n = xmalloc(sizeof *n);
	n->state = STATE_NEW;
	n->key = *key;
	n->ip = *iphdr;
	if (iphdr->protocol == PROTOCOL_TCP)
		n->tl.tcp = *tcphdr;
	else if (iphdr->protocol == PROTOCOL_UDP)
		n->tl.udp = *udphdr;
	else {
		assert(1);	// never happen
	}
	n->fid = fid;
	n->frag_id = 0;
	n->soft_timeout = ts_msec() + SOFT_TIMEOUT_INTERVAL;
	hmap_insert(&flow_hmap, &n->node, hval);
}

frag_node_t* build_frag_hmap(u16 ipid, flow_node_t* flow)
{
	frag_node_t *n;
	uint32_t h;

	h = hash_bytes(&ipid, sizeof ipid);

	n = xmalloc(sizeof *n);
	n->id = flow->frag_id++ % (1<N_FRAG_ID_BITS);
	n->ipid = ipid;
	n->flow = flow;
	hmap_insert(&frag_hmap, &n->node, h);

	return n;
}

void touch_flow(flow_node_t* flow)
{
	flow->soft_timeout = ts_msec() + SOFT_TIMEOUT_INTERVAL;
}

void add_to_pkt_list(struct list *pkt_list, void *data, u16 len)
{
	pkt_t *pkt;

	pkt = xmalloc(sizeof *pkt);
	pkt->len = len;
	memcpy(pkt->data, data, len);
	list_push_back(pkt_list, &pkt->node);
}

void build_ctl(struct list *pkt_list, flow_node_t *flow)
{
	pkt_t *pkt;
	chdr_t *ctl_hdr;

	pkt = xmalloc(sizeof *pkt);
	ctl_hdr = pkt->data;
	ctl_hdr->t = CTYPE_FRM_CTL;
	ctl_hdr->i = CTYPE_CTL_REQ;
	ctl_hdr->smac = MYMAC;
	ctl_hdr->id = flow->fid;
	pkt->len = sizeof *ctl_hdr;
	memcpy(pkt->data + pkt->len, flow->ip, sizeof flow->ip);
	pkt->len += sizeof flwo->ip;
	if (ip->protocol == PROTOCOL_TCP) {
		memcpy(pkt->data + pkt->len, flow->tl.tcp, sizeof flow->tl.tcp);
		pkt->len += sizeof flwo->tl.tcp;
	}
	else if (ip->protocol == PROTOCOL_UDP) {
		memcpy(pkt->data + pkt->len, flow->tl.tcp, sizeof flow->tl.tcp);
		pkt->len += sizeof flwo->tl.tcp;
	}
	else {
		assert(1);	// never happen
	}

	list_push_back(pkt_list, &pkt->node);
}

void build_normal(struct list *pkt_list, flow_node_t *flow, void *ippkt)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;
	struct tcphdr *tcphdr = (struct tcphdr *)(ippkt + sizeof struct iphdr);
	struct udphdr *udphdr = (struct udphdr *)(ippkt + sizeof struct iphdr);

	pkt_t *pkt;
	chdr_t *common_hdr;

	pkt = xmalloc(sizeof *pkt);
	common_hdr = pkt->data;
	common_hdr->i = 0;
	common_hdr->smac = MYMAC;
	common_hdr->id = flow->fid;
	pkt->len = sizeof *common_hdr;

	if (iphdr->protocol == PROTOCOL_TCP) {
		common_hdr->t = CTYPE_FRM_TCP;

		if (tcphdr->urg) {
			if (tcphdr->syn)
				common_hdr->i |= CTYPE_TCP_URGSYN;
			else if (tcphdr->fin)
				common_hdr->i |= CTYPE_TCP_URGFIN;
			else if (tcphdr->psh)
				common_hdr->i |= CTYPE_TCP_URGPSH;
			else
				common_hdr->i |= CTYPE_TCP_URG;
		}
		else {
			if (tcphdr->syn)
				common_hdr->i |= CTYPE_TCP_SYN;
			else if (tcphdr->fin)
				common_hdr->i |= CTYPE_TCP_FIN;
			else if (tcphdr->psh)
				common_hdr->i |= CTYPE_TCP_PSH;
			else
				assert(1);
		}

		common_hdr->i |= CTYPE_TCP_SBIT;
		memcpy(pkt->data + pkt->len, &tcphdr->seq, sizeof tcphdr->seq);
		pkt->len += sizeof tcphdr->seq;

		if (tcphdr->ack) {
			common_hdr->i |= CTYPE_TCP_ABIT;
			memcpy(pkt->data + pkt->len, &tcphdr->ack_seq, sizeof tcphdr->ack_seq);
			pkt->len += sizeof tcphdr->ack_seq;
		}

		common_hdr->i |= CTYPE_TCP_WBIT;
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
			iphdr->tot_len - ippkt + sizeof *iphdr + sizeof *tcphdr);
		pkt->len += iphdr->tot_len - sizeof *iphdr - sizeof *tcphdr;
	}
	else if (iphdr->protocol == PROTOCOL_UDP) {
		common_hdr->t = CTYPE_FRM_UDP;

		memcpy(pkt->data + pkt->len, &udphdr->check, sizeof udphdr->check);
		pkt->len += sizeof udphdr->check;

		memcpy(pkt->data + pkt->len,
			ippkt + sizeof *iphdr + sizeof *udphdr,
			iphdr->tot_len - ippkt + sizeof *iphdr + sizeof *udphdr);
		pkt->len += iphdr->tot_len - sizeof *iphdr - sizeof *udphdr;
	}
	else {
		assert(1);	// never happen
	}

	list_push_back(pkt_list, &pkt->node);
}

void build_frag(struct list *pkt_list, frag_node_t *frag, void *ippkt)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;

	pkt_t *pkt;
	chdr_t *common_hdr;

	pkt = xmalloc(sizeof *pkt);
	common_hdr->t = CTYPE_FRM_RAW;
	common_hdr = pkt->data;
	common_hdr->i = 0;
	common_hdr->smac = MYMAC;
	common_hdr->id = flow->fid;
	pkt->len = sizeof *common_hdr;

	common_hdr->i |= CTYPE_RAW_FBIT;
	if (iphdr->frag_off & FRAG_FLAG_MF_MASK)
		common_hdr->i |= CTYPE_RAW_MFBIT;
	common_hdr->i |= frag->id;

	memcpy(pkt->data + pkt->len,
		&iphdr->frag_off & FRAG_OFF_MASK,
		sizeof iphdr->frag_off);
	pkt->len += sizeof iphdr->frag_off;

	memcpy(pkt->data + pkt->len,
			ippkt + sizeof *iphdr
			iphdr->tot_len - sizeof *iphdr);
	pkt->len += iphdr->tot_len - sizeof *iphdr;

	list_push_back(pkt_list, &pkt->node);
}

void timer_event()
{
	flow_node_t *flow;
	long long int now = ts_msec();

	HMAP_FOR_EACH(flow, node, &flow_hmap) {
		if (now < flow->soft_timeout)
			continue;

		// timeout
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
	}
}

void xmit_compress(void *ippkt, struct list *pkt_list)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;
	struct tcphdr *tcphdr = (struct tcphdr *)(ippkt + sizeof struct iphdr);
	struct udphdr *udphdr = (struct udphdr *)(ippkt + sizeof struct iphdr);

	flow_node_t *flow;

	list_init(pkt_list);

	if (iphdr->version != 4 || iphdr->ihl != 5) {
		// not ipv4 or has ip options or (not tcp or udp)
		add_to_pkt_list(pkt_list, ippkt, iphdr->tot_len);
		return;
	}
	if (iphdr->protocol != PROTOCOL_TCP && iphdr->protocol != PROTOCOL_UDP) {
		add_to_pkt_list(pkt_list, ippkt, iphdr->tot_len);
		return;
	}

	frag = NULL;
	flow = NULL;

	if (iphdr->frag_off & FRAG_OFF_MASK) {
		// in the middle of fragmentation
		frag = locate_in_frag_hmap(iphdr->id);
		if (!frag) {
			// the first packet of the frag group is missed,
			// we can't handle it
			add_to_pkt_list(pkt_list, ippkt, iphdr->tot_len);
			return;	
		}
	}
	else {
		// not in middle of fragmentation
		// i.e. new flow / first pkt in frag ip pkgs

		// hash key is a 5-tuple
		key = get_flow_key(ippkt);
		flow_hval = hash_bytes(&key, sizeof key);

		flow = get_flow_by_hash(flow_hval, key);

		if (!flow) {	// new flow
			u16 k;
			u8 fid;

			fid = get_id(idp);
			// reverse-locate flow entry also via fid

			flow = build_flow_by_hash(flow_hval, &key, ippkt, fid);
			flow_rindex[fid] = flow;
		}
		if (iphdr->frag_off & FRAG_FLAG_MF_MASK) {
			// first pkt of a fragmentation group
			frag = build_frag_hmap(iphdr->id, flow);
		}
	}

	assert(frag | flow);

	if (flow)
		flow = frag->flow;
	switch (flow->state) {
		case STATE_NEW:
			build_ctl(pkt_list, flow);
			add_to_pkt_list(pkt_list, ippkt, iphdr->tot_len);
			flow->state = PENDING;
			break;
		case STATE_PENDING:
			add_to_pkt_list(pkt_list, ippkt, iphdr->tot_len);
			break;
		case STATE_ESTABLISHED:
	if (iphdr->frag_off & FRAG_OFF_MASK) {
		build_frag(pkt_list, frag, ippkt);
	}
	else {
	}
			cpkt = build(pkt);
			send(cpkt);
			break;
		default:
			assert(1);	// never happen
			break;
	}

	touch_flow(flow);
}

// return actural length in ippkt
int recv_compress(void *cpkt, void *ippkt, int size)
{
	struct iphdr *iphdr = (struct iphdr *)cpkt;
	chdr_t *chdr = (chdr_t *)cpkt;

	flow_node_t *flow;
	int len;

	list_init(pkt_list);

	if (iphdr->version == 4 && iphdr->ihl >= 5) {
		if (!csum(iphdr, sizeof *iphdr)) {
			// ipv4
			len = size >= iphdr->tot_len ? iphdr->tot_len:size;
			memcpy(ippkt, cpkt, len);
			return len;
		}
	}

	switch (chdr->t) {
		case CTYPE_FRM_CTL:
			len = recv_ctl(cpkt, ippkt, size);
			break;
		case CTYPE_FRM_TCP:
			len = recv_tcp(cpkt, ippkt, size);
			break;
		case CTYPE_FRM_UDP:
			len = recv_udp(cpkt, ippkt, size);
			break;
		case CTYPE_FRM_RAW:
			len = recv_raw(cpkt, ippkt, size);
			break;
		default:
			assert(1);
	}

	return 0;
}

int recv_ctl(void *cpkt, void *ippkt, int size)
{
	chdr_t *chdr = (chdr_t *)cpkt;
	int len;

	return len;
}

int recv_tcp(void *cpkt, void *ippkt, int size)
{
	chdr_t *chdr = (chdr_t *)cpkt;
	int len;

	return len;
}
int recv_udp(void *cpkt, void *ippkt, int size)
{
	chdr_t *chdr = (chdr_t *)cpkt;
	int len;

	return len;
}
int recv_raw(void *cpkt, void *ippkt, int size)
{
	chdr_t *chdr = (chdr_t *)cpkt;
	int len;

	return len;
}

