/*
 * hitload.c
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
 * Notes:- 
 * 
 * This is same as freeload apart from pulsewidths.
 *
 */


#include "../mydefs.h"
#include "../main.h"

#include <stdlib.h>

#define HDSZ 4


void hitload_search(void)
{
	int i, sof, sod, eod, eof;
	int z, lstart, lend, tmp, tcnt, hd[HDSZ];

	if (!quiet)
		msgout("  Hitload");
         

	for (i = 20; i < tap.len - 8; i++) {
		if ((z = find_pilot(i, HITLOAD)) > 0) {
			sof = i;
			i = z;
			if (readttbyte(i, ft[HITLOAD].lp, ft[HITLOAD].sp, ft[HITLOAD].tp, ft[HITLOAD].en)==ft[HITLOAD].sv) {
				sod = i + 8;

				/* decode the header, so we can validate the addresses */
				for (tcnt = 0; tcnt < HDSZ; tcnt++) {
					hd[tcnt] = readttbyte(sod + (tcnt * 8), ft[HITLOAD].lp, ft[HITLOAD].sp, ft[HITLOAD].tp, ft[HITLOAD].en);
					if (hd[tcnt] == -1)
						break;
				}
				if (tcnt != HDSZ)
					continue;

				lstart = hd[0] + (hd[1] << 8);	/* get start address */
				lend = hd[2] + (hd[3] << 8);	/* get end address */

				if (lend>lstart) {
					tmp = lend - lstart;	/* calculate last pulse from these addresses */
					tmp *= 8;
					eod = sod + tmp + (5 * 8) - 8;
					eof = eod + 7;

					addblockdef(HITLOAD, sof, sod, eod, eof, 0);

					i = eof;	/* optimize search */
				}
			}
		}
	}
}

int hitload_describe(int row)
{
	int i, s;
	int b, hd[HDSZ], rd_err, cb;

	s = blk[row]->p2;	/* decode the header... */
	for (i = 0; i < HDSZ; i++)
		hd[i] = readttbyte(s + (i * 8), ft[HITLOAD].lp, ft[HITLOAD].sp, ft[HITLOAD].tp, ft[HITLOAD].en);

	blk[row]->cs = hd[0] + (hd[1] << 8);			/* record start address */
	blk[row]->ce = hd[2] + (hd[3] << 8) - 1;		/* record end address */
	blk[row]->cx = (blk[row]->ce - blk[row]->cs) + 1;	/* record length in bytes */

	/* get pilot & trailer lengths */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1 - 8) >> 3;
	blk[row]->trail_len = (blk[row]->p4 - blk[row]->p3 - 7) >> 3;
        
	/* extract data and test checksum... */
	rd_err = 0;
	cb = 0;
	s = (blk[row]->p2) + (HDSZ * 8);

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);
	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);
   
	for (i = 0; i < blk[row]->cx; i++) {
		b = readttbyte(s + (i * 8), ft[HITLOAD].lp, ft[HITLOAD].sp, ft[HITLOAD].tp, ft[HITLOAD].en);
		cb ^= b;
		if (b == -1)
			rd_err++;
		blk[row]->dd[i] = b;
	}

	b = readttbyte(s + (i * 8), ft[HITLOAD].lp, ft[HITLOAD].sp, ft[HITLOAD].tp, ft[HITLOAD].en);	/* read checksum */

	blk[row]->cs_exp = cb & 0xFF;
	blk[row]->cs_act = b;
	blk[row]->rd_err = rd_err;

	return 0;
}
