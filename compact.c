
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
	u8 fid;		// compact-ipid
	index_node_t* index;
	int soft_timeout;
}flow_node_t;

typedef struct {
	struct hmap_node node;
	u16 ipid;
	flow_node_t *flow;
}frag_node_t;

typedef struct {
	struct hmap_node node;
	u8 fid;
	flow_node_t *flow;
}index_node_t;

struct hmap flow_hmap;
struct hmap frag_hmap;
struct hmap index_hmap;

idpool_t *idp;

void compact_init()
{
	hmap_init(&flow_hmap);
	hmap_init(&frag_hmap);
	hmap_init(&index_hmap);

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
	n->index = NULL;
	n->soft_timeout = ts_msec() + SOFT_TIMEOUT_INTERVAL;
	hmap_insert(&flow_hmap, &n->node, hval);
}

uint32_t get_index_hash(u8 dstmac, u8 fid)
{
	uint32_t h;
	u16 t;

	t = dstmac;
	t <<= 8;
	t |= fid;
	h = hash_bytes(&t, sizeof t);

	return h;
}

index_node_t* build_index_by_hash(u8 dstmac, u8 fid)
{
	index_node_t *n;
	uint32_t h;

	h = get_index_hash(destmac, fid);

	n = xmalloc(sizeof *n);
	n->fid = fid;
	n->flow = NULL;
	hmap_insert(&index_hmap, &n->node, h);

	return n;
}

frag_node_t* build_frag_hmap(u16 ipid, flow_node_t* flow)
{
	frag_node_t *n;
	uint32_t h;

	h = hash_bytes(&ipid, sizeof ipid);

	n = xmalloc(sizeof *n);
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

xmit_compress(u8 dstmac, void *ippkt, struct list *pkt_list)
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
			flow = build_flow_by_hash(flow_hval, &key, ippkt, fid);
			// reverse-locate flow entry via compact-ipid
			index = build_index_by_hash(dstmac, fid);

			flow->index = index;
			index->flow = flow;
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
			ctlpkt = build_ctl(ippkt);
			send(ctlpkt);
			flow_state = PENDING;
			send(ippkt);
			break;
		case STATE_PENDING:
			send(ippkt);
			break;
		case STATE_ESTABLISHED:
			cpkt = build(pkt);
			send(cpkt);
			break;
		default:
			assert(1);	// never happen
			break;
	}

	touch_flow(flow);
}

void build_ctrl(ippkt)
{
	struct iphdr *iphdr = (struct iphdr *)ippkt;
	struct tcphdr *tcphdr = (struct tcphdr *)(ippkt + sizeof struct iphdr);
	struct udphdr *udphdr = (struct udphdr *)(ippkt + sizeof struct iphdr);

}

build(pkt)
{
	if (fragment)
		ret = build_frag(pkt);
	else if (tcp)
		ret = build_tcp(pkt);
	else if (udp)
		ret = build_udp(pkt);
}

build_tcp(pkt)
{
}

build_frag(pkt)
{
}

build_udp(pkt)
{
}

timer_event()
{
	FOR_EACH_IN_HASH_TABLE() {
	switch (flow_state) {
		case NEW:
			break;
		case PENDING:
			if (timout1)
				remove_flow();
			break;
		case ESTB:
			if (timout2)
				remove_flow();
			break;
		default:
			break;
	}
	}
}

on_recv(pkt)
{
	if (crl)
		process_ctl(pkt);
	else
		process_data(pkt);
}

process_ctl(pkt)
{
	if (compact)
}

process_data(pkt)
{
	if (compact)
}

