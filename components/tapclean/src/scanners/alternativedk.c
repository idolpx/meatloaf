/*
 * alternativedk.c (by Luigi Di Fraia, May 2011)
 * Based on alternativesw.c and chr.c
 *
 * Part of project "TAPClean". May be used in conjunction with "Final TAP".
 *
 * Final TAP is (C) 2001-2006 Stewart Wilson, Subchrist Software.
 *
 *
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
 * St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * Status: Beta
 *
 * CBM inspection needed: No
 * Single on tape: No
 * Sync: 3 bits whose value is irrelevant
 * Header: Yes
 * Data: Continuous
 * Checksum: Yes
 * Post-data: No
 * Trailer: Yes
 * Trailer homogeneous: No
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZEBITS	3	/* amount of sync bits */
#define MAXTRAILER	16	/* max amount of trailer pulses read in */

/*
 * Find custom pilot sequence (lp x 0x1F, mp x 0x01)
 */
static int alternativedk_find_pilot (int variant, int pos)
{
	int z, lp, mp, tp, pmin;

	lp = ft[variant].lp;
	mp = ft[variant].mp;
	pmin = ft[variant].pmin;

	tp = (mp + lp) / 2;

	if (readttbit(pos, lp, mp, tp) == 1) {
		z = 0;

		while (readttbit(pos, lp, mp, tp) == 1 && pos < tap.len) {
			z++;
			pos++;
		}

		if (z == 0)
			return 0;

		if (z < pmin)
			return -pos;

		if (z >= pmin)
			return pos;
	}

	return 0;
}

static void alternativedk_search_core(int lt)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */

	int en, tp, sp, mp, sv;		/* encoding parameters */

	unsigned int s;			/* block location referred to C64 memory */
	unsigned int x; 		/* block size */

	int xinfo;			/* extra info used in addblockdef() */


	en = ft[lt].en;
	tp = ft[lt].tp;
	sp = ft[lt].sp;
	mp = ft[lt].mp;
	sv = ft[lt].sv;

	if (!quiet) {
		sprintf(lin, "  Alternative SW (DK) T%d", lt - ALTERDK_T1 + 1);
		msgout(lin);
	}

	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {
		eop = alternativedk_find_pilot(lt, i);

		if (eop > 0) {
			/* Valid pilot found, mark start of file */
			sof = i;
			i = eop;

			/* Check if there's a valid sync bit for this loader */
			if (readttbit(i, mp, sp, tp) != sv)
				continue;

			/* Check for 2 valid bits to finish off the sync sequence */
			if (readttbit(i, mp, sp, tp) < 0)
				continue;

			if (readttbit(i, mp, sp, tp) < 0)
				continue;

			/* Take into account sync bits */
			i += SYNCSEQSIZEBITS;

			/* Valid sync train found, mark start of data */
			sod = i;

			/* Read load address MSB */
			s = readttbyte(sod, mp, sp, tp, en);
			if (s == -1)
				continue;

			/* Set size */
			x = 0x100;

			/* Point to the first pulse of the checkbyte (that's final) */
			eod = sod + x * BITSINABYTE;

			/* Initially point to the last pulse of the checkbyte */
			eof = eod + BITSINABYTE - 1;

			/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, mp, sp, tp) >= 0)
				eof++;

			/* Store the load address MSB as extra-info */
			xinfo = (int) s;

			if (addblockdef(lt, sof, sod, eod, eof, xinfo) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

void alternativedk_search(int lt)
{
	if (lt > 0) {
		alternativedk_search_core(lt);
	} else {
		int type, types[] = { ALTERDK_T1, ALTERDK_T2, ALTERDK_T3, ALTERDK_T4 };

		for (type = 0; type < sizeof(types)/sizeof(types[0]); type++) {
			alternativedk_search_core(types[type]);
		}
	}
}

int alternativedk_describe (int row)
{
	int i, s;
	int en, tp, sp, mp;
	int cb;

	int b, rd_err;

	int variant = blk[row]->lt;

	en = ft[variant].en;
	tp = ft[variant].tp;
	sp = ft[variant].sp;
	mp = ft[variant].mp;

	/* Retrieve C64 memory location for load address from extra-info */
	blk[row]->cs = blk[row]->xi << 8;
	blk[row]->ce = blk[row]->cs + 0x100 - 1;
	blk[row]->cx = 0x100;

	/* Give extra info if this is a marker block */
	if (blk[row]->xi <= 2) {
		strcat(info,"\n - Marker block");
		strcat(info,"\n - Type: ");

		switch (blk[row]->xi) {
			case 0x00:
				strcat(info,"End of load");
				break;
			case 0x01:
				strcat(info,"End of file");
				break;
			case 0x02:
				strcat(info,"End of tape");
				break;
		}
	}

	/* Compute pilot & trailer lengths */

	/* pilot is in pulses... */
	blk[row]->pilot_len = blk[row]->p2 - blk[row]->p1;

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync sequence (3 bits) */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= SYNCSEQSIZEBITS;

	/* Extract data and test checksum... */
	rd_err = 0;
	cb = 0;

	s = blk[row]->p2 + BITSINABYTE;	/* Point to data, after load address MSB */

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);

	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

	for (i = 0; i < blk[row]->cx; i++) {
		b = readttbyte(s + (i * BITSINABYTE), mp, sp, tp, en);
		cb ^= b;

		if (b != -1) {
			blk[row]->dd[i] = b;
		} else {
			blk[row]->dd[i] = 0x69;	/* sentinel error value */
			rd_err++;

			/* for experts only */
			sprintf(lin, "\n - Read Error on byte @$%X (prg data offset: $%04X)", s + (i * BITSINABYTE), i + 2);
			strcat(info, lin);
		}
	}

	b = readttbyte(s + (i * BITSINABYTE), mp, sp, tp, en);
	if (b == -1) {
		/* Even if not within data, we cannot validate data reliably if
		   checksum is unreadable, so that increase read errors */
		rd_err++;

		/* for experts only */
		sprintf(lin, "\n - Read Error on checkbyte @$%X", s + (i * BITSINABYTE));
		strcat(info, lin);
	}

	blk[row]->cs_exp = cb & 0xFF;
	blk[row]->cs_act = b  & 0xFF;
	blk[row]->rd_err = rd_err;

	return(rd_err);
}
