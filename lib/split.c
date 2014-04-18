
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

	if (p - frm + 1 > size)
		goto err_out;
	*(++p) = BREAK_CHAR;

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
 *   -1		buf size's too small
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

	if (*ch != BREAK_CHAR || ch-frm + 1 > frm_len)
		goto err_out;
	++ch;

	find_start_break = 0;
	while (ch-frm < frm_len) {
		if (!find_start_break && *(ch++) != BREAK_CHAR)
			continue;

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
					goto out;	// err: escape followed
							//      by nothing
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
					goto err_out;
				*(p++) = *ch;
		}

		// parse next char
		++ch;
	}
	len = -1;

out:
	return len;
}
