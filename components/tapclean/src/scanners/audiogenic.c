/*
 * audiogenic.c
 *
 * Part of project "Final TAP". 
 *
 * A Commodore 64 tape remastering and data extraction utility.
 *
 * (C) 2001-2006 Stewart Wilson, Subchrist Software.
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


#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define HDSZ 1


void audiogenic_search(void)
{
	int i, sof, sod, eod, eof;
	int z;
	int en, tp, sp, lp, sv;

	en = ft[AUDIOGENIC].en;
	tp = ft[AUDIOGENIC].tp;
	sp = ft[AUDIOGENIC].sp;
	lp = ft[AUDIOGENIC].lp;
	sv = ft[AUDIOGENIC].sv;

	if (!quiet)
		msgout("  Audiogenic");

	for (i = 20; i < tap.len - 8; i++) {
		if ((z = find_pilot(i, AUDIOGENIC)) > 0) {
			sof = i;
			i = z;
			if (readttbyte(i, lp, sp, tp, en) == sv) {

				/* printf("\nFound AUDIOGENIC pilot + sync @ $%04X",i);  */

				sod = i + 8; /* 1 byte is the sync value... */

				/* ... then 1 byte is the header size and 256 is data size = 257 bytes...
				   ... the trailing 0x01 value is broken for the last file in a chain.
				   This way eod points to the first bit of the checkbyte... (luigi) */
				eod = sod + ((HDSZ+256) * 8);

				eof = eod + 7;	/* last bit of the check byte */

				/* check if there's a whole trailer byte */
				if (readttbyte(eof + 1, lp, sp, tp, en) == 0x01)
					eof +=8;
				/* broken trailer byte: trace 'eof' to end of trailer (bit 0 only of course) */
				else if (eof > 0)
					while (eof < tap.len - 1 && readttbit(eof + 1, lp, sp, tp) == 0)
						eof++;

				if (addblockdef(AUDIOGENIC, sof, sod, eod, eof, 0) >= 0)
					i = eof;
			}
		} else {
			if (z < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-z);
		}
	}
}

int audiogenic_describe(int row)
{
	int i,s,b,cb;
	int en,tp,sp,lp;

	en = ft[AUDIOGENIC].en;
	tp = ft[AUDIOGENIC].tp;
	sp = ft[AUDIOGENIC].sp;
	lp = ft[AUDIOGENIC].lp;
 
	
	b = readttbyte(blk[row]->p2, lp, sp, tp, en);
	blk[row]->cs = b << 8;
	blk[row]->ce = blk[row]->cs + 255;
	blk[row]->cx = 256;

	/* block type (luigi) */

	strcat(info, "\n - Block type: ");
	if (b == 0 || b == 2)
		strcat(info, "execute");
	else if (b == 1)
		strcat(info, "re-sync");
	else if (b == 0xCF)
		strcat(info, "loader (part 2)");
	else
		strcat(info, "data");

	/* get pilot & trailer lengths */

	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1 - 8) >> 3;
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - 7;

	/* show trailing byte */

	b = readttbyte(blk[row]->p2 + (258 * 8), lp, sp, tp, en);
	if (b != -1) { /* (luigi) */
		sprintf(lin, "\n - Trailer byte (not broken) : $%02X", b);
		strcat(info, lin);
	}

	/* extract data and test checksum... */

	cb = 0;
	s = blk[row]->p2 + (HDSZ * 8);

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);

	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

	for (i = 0; i < blk[row]->cx; i++) {
		b = readttbyte(s + (i * 8), lp, sp, tp, en);
		cb ^= b;
		if (b == -1)
			blk[row]->rd_err++;
		blk[row]->dd[i] = b;
	}
	
	b = readttbyte(s + (i * 8), lp, sp, tp, en);	/* read actual checkbyte. */

	blk[row]->cs_exp = cb & 0xFF;
	blk[row]->cs_act = b;

	return 0;
}

