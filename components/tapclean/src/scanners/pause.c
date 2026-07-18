/*
 * pause.c
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

void pause_search(void)
{
	int i, start, dr = 0;
	unsigned char b;

	if (!quiet)
		msgout("  Pauses");

	for (i = 20; i < tap.len && dr != DBFULL; i++) {
		b = tap.tmem[i];
		if (b == 0) {			/* found a pause... */
			if (tap.version == 1) {
				dr = addblockdef(PAUSE, i, 0, 0, i + 3, 0);
				i += 3;		/* skip to end of pause */
			}
			if (tap.version == 0) {
				start = i;
				while (tap.tmem[i] == 0 && i < tap.len)	/* skip to end of pause */
					i++;
				dr = addblockdef(PAUSE, start, 0, 0, i - 1, 0);
			}
		}
	}
}

void pause_describe(int row)
{
	int i, val;
	float sec;
      
	if (blk[row]->lt == PAUSE) {

		/* find its length in cycles (v0)... */

		if (tap.version == 0) {
			i = blk[row]->p1;	/* load the start address */
			val = 0;
			while(tap.tmem[i++] == 0 && i < tap.len)
				val += 20000;
		}

		/* find its length in cycles (v1)... */

		if (tap.version == 1) {
			i = blk[row]->p1;	/* load the start address */
			val = tap.tmem[i + 1] + (tap.tmem[i + 2] << 8) + (tap.tmem[i + 3] << 16) ;
		}

		/* show results... */

		sec = (float)val / 985248;
		sprintf(lin, "\n - Length : %u cycles (%0.4f secs)", val, sec);
		strcat(info, lin);
	}
}
