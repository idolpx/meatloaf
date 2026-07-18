/*
 * ikloader.c (International Karate Format)
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
#include <stdio.h>
#include <stdlib.h>

#define HDSZ 7


void ik_search(void)
{
	int i, j, sof, sod, eod, eof;
	int z, b, hd[HDSZ];
	int s, e, x;

	if (!quiet)
		msgout("  International Karate loader");
         

	for (i = 20; i < tap.len - 20; i++) {
		if ((z = find_pilot(i, IK)) > 0) {
			sof = i;
			i = z;

			if (readttbit(i, ft[IK].lp, ft[IK].sp, ft[IK].tp) == ft[IK].sv) {	/* got sync bit? */
				i++;
				b = readttbyte(i, ft[IK].lp, ft[IK].sp, ft[IK].tp, ft[IK].en);
				if (b < 10) {
					sod = i;

					/* decode the header, so we can validate the addresses... */
					for (j = 0; j < HDSZ; j++) {
						hd[j] = readttbyte(sod + (j * 8), ft[IK].lp, ft[IK].sp, ft[IK].tp, ft[IK].en);
						if (hd[j] == -1)
							break;
					}
					if (j != HDSZ)
						continue;

					s = hd[1] + (hd[2] << 8);	/* get start address */
					e = hd[3] + (hd[4] << 8);	/* get end address */

					if (e > s) {
						x = (e - s) + 1;
						eod = sod + ((x + HDSZ) * 8);
						eof = eod + 7;
						addblockdef(IK, sof, sod, eod, eof, 0);
						i = eof;	/* optimize search */
					}
				}
			}
		} else {
			if (z < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-z);
		}
	}
}

int ik_describe(int row)
{
	int i, s, b, hd[HDSZ], rd_err, cb;

	/* decode the header... */
	s = blk[row]->p2;
	for (i = 0; i < HDSZ; i++)
		hd[i] = readttbyte(s + (i * 8), ft[IK].lp, ft[IK].sp, ft[IK].tp, ft[IK].en);

	blk[row]->cs = hd[1] + (hd[2] << 8);
	blk[row]->ce = hd[3] + (hd[4] << 8);
	blk[row]->cx = (blk[row]->ce - blk[row]->cs) + 1;

	sprintf(lin, "\n - Exe Address : $%04X", hd[5] + (hd[6] << 8));
	strcat(info, lin);

	/* get pilot & trailer lengths... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1);
	blk[row]->trail_len = (blk[row]->p4 - blk[row]->p3) - 7;

	/* extract data and test checksum... */
	rd_err = 0;
	cb = 0;
	s = (blk[row]->p2) + (HDSZ * 8);

	if (blk[row]->dd != NULL)
	free(blk[row]->dd);
	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);
   
	for (i = 0; i < blk[row]->cx; i++) {
		b = readttbyte(s + (i * 8), ft[IK].lp, ft[IK].sp, ft[IK].tp, ft[IK].en);
		cb ^= b;
		if (b == -1)
			rd_err++;
		blk[row]->dd[i] = b;
	}

	b = readttbyte(s + (i * 8), ft[IK].lp, ft[IK].sp, ft[IK].tp, ft[IK].en);	/* read actual cb */
	blk[row]->cs_exp = cb & 0xFF;
	blk[row]->cs_act = b;
	blk[row]->rd_err = rd_err;

	return 0;
}
