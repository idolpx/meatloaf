/*
 * jiffyload.c (by Luigi Di Fraia, Sep 2009)
 * Based on ashdave.c
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
 * Single on tape: Yes
 * Sync: Byte
 * Header: Yes
 * Data: Continuous
 * Checksum: No
 * Post-data: Yes
 * Trailer: No
 * Trailer homogeneous: N/A
 */

/*
 * Note: Due to loss of precision in the TAP format, the threshold for this
 *       loader has been set to 0x1D rather than 0x1E. Some bit 1 pulses
 *       have been found shifted down to 0x1E.
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	1	/* amount of sync bytes */
#define MAXTRAILER	8	/* max amount of trailer pulses read in */

#define HEADERSIZE	6	/* size of block header */

#define LOADOFFSETH	0	/* load location (MSB) offset inside header */
#define LOADOFFSETL	1	/* load location (LSB) offset inside header */
#define ENDOFFSETH	2	/* end  location (MSB) offset inside header */
#define ENDOFFSETL	3	/* end  location (LSB) offset inside header */

#define POSTDATASIZE	256	/* size in bytes of the MANDATORY information
				   that is found after file data */

static void jiffyload_search_core (int lt)
{
	int i, h;			/* counters */
	unsigned char j;
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */

	int en, tp, sp, lp, sv;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */


	en = ft[lt].en;
	tp = ft[lt].tp;
	sp = ft[lt].sp;
	lp = ft[lt].lp;
	sv = ft[lt].sv;

	if (!quiet) {
		sprintf(lin, "  Jiffy Load T%d", lt - JIFFYLOAD_T1 + 1);
		msgout(lin);
	}

	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {
		eop = find_pilot(i, lt);

		if (eop > 0) {
			/* Valid pilot found, mark start of file */
			sof = i;
			i = eop;

			/* Check if there's a valid sync byte for this loader */
			if (readttbyte(i, lp, sp, tp, en) != sv)
				continue;

			/* Valid sync found, mark start of data */
			sod = i + SYNCSEQSIZE * BITSINABYTE;

			/* Read header */
			for (h = 0; h < HEADERSIZE; h++) {
				hd[h] = readttbyte(sod + h * BITSINABYTE, lp, sp, tp, en);
				if (hd[h] == -1)
					break;
			}

			/* Bail out if there was an error reading the block header */
			if (h != HEADERSIZE)
				continue;

			/* Extract load and end locations */
			s = hd[LOADOFFSETL] + (hd[LOADOFFSETH] << 8);
			e = hd[ENDOFFSETL]  + (hd[ENDOFFSETH]  << 8);

			/* Prevent int wraparound when subtracting 1 from end location
			   to get the location of the last loaded byte */
			if (e == 0)
				e = 0xFFFF;
			else
				e--;

			/* Plausibility check */
			if (e < s)
				continue;

			/* Compute size */
			x = e - s + 1;

			/* Point to the first pulse of the last data byte (that's final) */
			eod = sod + (HEADERSIZE + x - 1) * BITSINABYTE;

			/* Point to the first pulse of the first postdata byte */
			eof = eod + BITSINABYTE;

			/* Decode a POSTDATASIZE byte sequence (possibly a valid postdata pattern) */
			for (h = 0, j = 0; h < POSTDATASIZE; h++, j--)
				if (readttbyte(eof + (h * BITSINABYTE), lp, sp, tp, en) != (int)j)
					break;

			/* Postdata pattern doesn't match */
			if (h != POSTDATASIZE)
				continue;

			/* Point to the last pulse of the last postdata byte */
			eof = eod + (POSTDATASIZE + 1) * BITSINABYTE - 1;

			/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
			/* Note: No trailer has been documented, but we are not strictly
			         requiring one here, just checking for it is future-proof */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) >= 0)
				eof++;

			if (addblockdef(lt, sof, sod, eod, eof, 0) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

void jiffyload_search (int lt)
{
	if (lt > 0) {
		jiffyload_search_core(lt);
	} else {
		int type, types[] = { JIFFYLOAD_T1, JIFFYLOAD_T2 };

		for (type = 0; type < sizeof(types)/sizeof(types[0]); type++) {
			jiffyload_search_core(types[type]);
		}
	}
}

int jiffyload_describe (int row)
{
	int i, s;
	int hd[HEADERSIZE];
	int en, tp, sp, lp;

	int b, rd_err;


	en = ft[blk[row]->lt].en;
	tp = ft[blk[row]->lt].tp;
	sp = ft[blk[row]->lt].sp;
	lp = ft[blk[row]->lt].lp;

	/* Set read pointer to the beginning of the payload */
	s = blk[row]->p2;

	/* Read header (it's safe to read it here for it was already decoded during the search stage) */
	for (i = 0; i < HEADERSIZE; i++)
		hd[i] = readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);

	/* Extract load and end locations */
	blk[row]->cs = hd[LOADOFFSETL] + (hd[LOADOFFSETH] << 8);
	blk[row]->ce = hd[ENDOFFSETL]  + (hd[ENDOFFSETH]  << 8);

	/* Prevent int wraparound when subtracting 1 from end location
	   to get the location of the last loaded byte */
	if (blk[row]->ce == 0)
		blk[row]->ce = 0xFFFF;
	else
		(blk[row]->ce)--;

	/* Compute size */
	blk[row]->cx = blk[row]->ce - blk[row]->cs + 1;

	/* Compute pilot & trailer lengths */

	/* pilot is in bytes... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / BITSINABYTE;

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync byte */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= SYNCSEQSIZE;

	/* Extract data */
	rd_err = 0;

	s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);

	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

	for (i = 0; i < blk[row]->cx; i++) {
		b = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, en);
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

	blk[row]->rd_err = rd_err;

	return(rd_err);
}
