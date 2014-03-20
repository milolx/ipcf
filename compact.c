
xmit_compress(ippkt)
{
	if (not ip)
		return ippkt;
	if (has ip options)
		return ippkt;
	if (not tcp && not udp)
		return ippkt;

	flow_entry = NULL;

	if (mf == 1)
		flow_entry = locate_in_frag_flow_list(ipid);

	if (!flow_entry) {
		if (frag_offset != 0)
			return ippkt;	// This may not happen
		key = get_key(orig_pkt);
		ip_hash = get_hash(key);
		flow_entry = get_flow_by_hash(ip_hash, key);
	}

	if (!flow_entry) {
		u16 t;

		id = get_id(ID_POOL);
		t = MYMAC;
		t <<= 8;
		t &= id;
		cflow_hash = get_hash(t);

		build_flow_by_hash(ip_hash, key, ippkt);
		build_index_by_hash(flow_hash, MYMAC, id, flow_entry);
		if (MF == 1)
			link_to_frag_flow_list(ipid);
	}
	switch (flow_state) {
		case NEW:
			ctlpkt = build_ctl(ippkt);
			send(ctlpkt);
			flow_state = PENDING;
			send(ippkt);
			break;
		case PENDING:
			send(ippkt);
			break;
		case ESTB:
			cpkt = build(pkt);
			send(cpkt);
			break;
		default:
			break;
	}
	touch_flow();	// timestamp for timeout
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

