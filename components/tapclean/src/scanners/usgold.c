/*
 * usgold.c
 *
 * Part of project "Final TAP". 
 *
 * A Commodore 64 tape remastering and data extraction utility.
 *
 * (C) 2001-2006 Stewart Wilson, Subchrist Software.
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

#define HDSZ 20

#define BLK_GENUINE          0
#define BLK_GAUNTLET_VARIANT 1

void usgold_search(void)
{
	int i, sof, sod, eod, eof;
	int z, tcnt, hd[HDSZ], x;

	if (!quiet)
		msgout("  US Gold tape");
         

	for (i = 20; i < tap.len - 8; i++) {
		if ((z = find_pilot(i, USGOLD)) > 0) {
			sof = i;
			i = z;
			if (readttbyte(i, ft[USGOLD].lp, ft[USGOLD].sp, ft[USGOLD].tp, ft[USGOLD].en) == ft[USGOLD].sv) {
				sod = i + 8;

				/* decode the header, so we can validate the addresses */

				for (tcnt = 0; tcnt < HDSZ; tcnt++) {
					hd[tcnt] = readttbyte(sod + (tcnt * 8), ft[USGOLD].lp, ft[USGOLD].sp, ft[USGOLD].tp, ft[USGOLD].en);
					if (hd[tcnt] == -1)
						break;
				}
				if (tcnt != HDSZ)
					continue;

				x = (hd[18] + (hd[19] << 8)) - (hd[16] + (hd[17] << 8));

				if (x > 0) {
					eod = sod + ((x + HDSZ) * 8);
					eof = eod + 7;
					if (addblockdef(USGOLD, sof, sod, eod, eof, BLK_GENUINE) >= 0)
						i = eof;

					/* This change has been done to detect the first file 
					   on Gauntlet side 2. The data size is 1 byte less 
					   than it should, probably it uses the end address
					   differently */
					else if (addblockdef(USGOLD, sof, sod, eod - 8, eof - 8, BLK_GAUNTLET_VARIANT) >= 0)
						i = eof - 8;
				}
			}
		}
	}
}

int usgold_describe(int row)
{
	int i,s;
	int hd[HDSZ], rd_err, b, cb;
	char fn[256];
	char str[2000];

	int xinfo;

	/* decode the header... */

	s = blk[row]->p2;
	for (i = 0; i < HDSZ; i++)
	hd[i] = readttbyte(s + (i * 8), ft[USGOLD].lp, ft[USGOLD].sp, ft[USGOLD].tp, ft[USGOLD].en);

	/* extract file name... */
	
	for (i = 0; i < 16; i++)
		fn[i] = hd[i];
	fn[i] = 0;
	trim_string(fn);
	pet2text(str, fn);

	if (blk[row]->fn != NULL)
		free(blk[row]->fn);
	blk[row]->fn = (char*)malloc(strlen(str) + 1);

	strcpy(blk[row]->fn, str);

	/* Retrieve the info about the block type (genuine/Gauntlet variant) */
	xinfo = blk[row]->xi;

	blk[row]->cs = hd[16] + (hd[17] << 8);		/* record start address */
	blk[row]->ce = hd[18] + (hd[19] << 8) - 1;	/* record end address */
	blk[row]->cx = (blk[row]->ce + 1)-blk[row]->cs;	/* compute size */

	/* Gauntlet side 2 full support */
	if (xinfo == BLK_GAUNTLET_VARIANT) {
		(blk[row]->ce) -= 1;
		(blk[row]->cx) -= 1;
	}

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
		b = readttbyte(s + (i * 8), ft[USGOLD].lp, ft[USGOLD].sp, ft[USGOLD].tp, ft[USGOLD].en);
		cb ^= b;
		if (b == -1)
			rd_err++;
		blk[row]->dd[i] = b;
	}
	b = readttbyte(s + (i * 8), ft[USGOLD].lp, ft[USGOLD].sp, ft[USGOLD].tp, ft[USGOLD].en);

	blk[row]->cs_exp = cb & 0xFF;
	blk[row]->cs_act = b;
	blk[row]->rd_err = rd_err;
	return 0;
}

