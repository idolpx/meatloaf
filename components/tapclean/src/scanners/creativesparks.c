/*
 * creativesparks.c (by Luigi Di Fraia, Aug 2014)
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
 * CBM inspection needed: Yes
 * Single on tape: Yes! -> once we acknowledge one, we can return (see comment below)
 * Sync: Sequence (bytes)
 * Header: Format signature only
 * Data: Sub-blocks
 * Checksum: Yes (for each sub-block)
 * Post-data: ?
 * Trailer: ?
 * Trailer homogeneous: ?
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	CSPARKS

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	3	/* amount of sync bytes */
#define MAXTRAILER	8	/* max amount of trailer pulses read in */

#define LOADOFFSETH	0x2C	/* load location (MSB) offset inside CBM header */
#define LOADOFFSETL	0x28	/* load location (LSB) offset inside CBM header */
#define DATAOFFSETH	0x24	/* data size (pages) offset inside CBM header */

#define SBLOCKSIZE	256	/* sub-block size, in bytes */

void creativesparks_search (void)
{
	int i, h;			/* counter */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int ib;				/* condition variable */

	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x, bso; 		/* block size and its overhead due to internal checksums */

	int xinfo;			/* extra info used in addblockdef() */

	/* Expected sync pattern */
	static int sypat[SYNCSEQSIZE] = {
		0xFF, 0xAA, 0xFF
	};


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	if (!quiet)
		msgout("  Creative Sparks");

	/*
	 * First we retrieve load variables from the CBM header.
	 * We use CBM HEAD index # 1 as we assume the tape image contains 
	 * a single game.
	 * For compilations we should search and find the relevant file 
	 * using the search code found e.g. in Biturbo.
	 */
	ib = find_decode_block(CBM_HEAD, 1);
	if (ib == -1)
		return;		/* failed to locate cbm header. */

	/* Basic validation before accessing array elements */
	if (blk[ib]->cx < LOADOFFSETH + 1)
		return;

	/* Extract load location and size */
	s = blk[ib]->dd[LOADOFFSETL] + (blk[ib]->dd[LOADOFFSETH] << 8); /* 0x0801 */
	x = blk[ib]->dd[DATAOFFSETH] << 8;

	/* Compute C64 memory location of the _LAST loaded byte_ */
	e = s + x - 1;

	/* Plausibility check */
	if (e < s || e > 0xFFFF)
		return;

	/* Compute size overhead due to internal checksums */
	bso = x / 256;
	if (x % 256)
		bso++;

	//printf ("\nLoad address: $%04X\nData Size: $%04X", s, x);

	/* Note: we exit the "for" cycle if addblockdef() doesn't fail,
	   that's because CS is always just ONE turbo file. */
	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {
		eop = find_pilot(i, THISLOADER);

		if (eop > 0) {
			/* Valid pilot found, mark start of file */
			sof = i;
			i = eop;

			/* Decode a SYNCSEQSIZE byte sequence (possibly a valid sync train) */
			for (h = 0; h < SYNCSEQSIZE; h++) {
				if (readttbyte(i + (h * BITSINABYTE), lp, sp, tp, en) != sypat[h])
					break;
			}

			/* Sync train doesn't match */
			if (h != SYNCSEQSIZE)
				continue;

			//printf ("\nSync train found @ %d", i);

			/* Valid sync train found, mark start of data */
			sod = i + SYNCSEQSIZE * BITSINABYTE;

			/* Point to the first pulse of the last checkbyte (that's final) */
			/* Note: - 1 because "bso" also includes the last checkbyte! */
			eod = sod + (x + bso - 1) * BITSINABYTE;

			/* Initially point to the last pulse of the last checkbyte */
			eof = eod + BITSINABYTE - 1;

			/* Trace 'eof' to end of trailer (bit 0 pulses only) */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) == 0)
				eof++;

			/* Store the info read from CBM part as extra-info */
			xinfo = s + (e << 16);

			if (addblockdef(THISLOADER, sof, sod, eod, eof, xinfo) >= 0)
				return;	/* We are done here */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int creativesparks_describe(int row)
{
	int i;
	int en, tp, sp, lp;
	int cb;

	int cnt, s, tot;
	int b, rd_err;
	int good, done, boff, blocks;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Retrieve C64 memory location for load/end address from extra-info */
	blk[row]->cs = blk[row]->xi & 0xFFFF;
	blk[row]->ce = (blk[row]->xi & 0xFFFF0000) >> 16;
	blk[row]->cx = (blk[row]->ce - blk[row]->cs) + 1;

	/* Compute the number of sub-blocks in this file */
	blocks = (blk[row]->cx) / 256;
	if ((blk[row]->cx) % 256)
		blocks++;

	/* Compute pilot & trailer lengths */

	/* pilot is in bytes... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / BITSINABYTE;

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync byte */
	if (blk[row]->pilot_len > 0) 
		blk[row]->pilot_len -= SYNCSEQSIZE;

	/* Print out the number of blocks */
	sprintf(lin, "\n - Sub-blocks : %d", blocks);
	strcat(info, lin);

	/* Test all sub-block checksums individually */
	s = blk[row]->p2;
	good = 0;
	blocks = 0;
	do {
		cnt = 0;
		done = 0;
		/* Note: Add 1 for checksum */
		boff = blocks * (SBLOCKSIZE + 1) * BITSINABYTE;
		cb = 0;
		do {
			b = readttbyte(s + boff + (cnt * BITSINABYTE), lp, sp, tp, en);
			cb ^= b;
			cnt++;
			
			/* Note: p3 points to the first pulse of the checkbyte */
			if (cnt == SBLOCKSIZE || s + boff + (cnt * BITSINABYTE) == blk[row]->p3) {
				/* we reached the checkbyte (257th byte or last one) */
				b = readttbyte(s + boff + (cnt * BITSINABYTE), lp, sp, tp, en);
				if (b == cb)
					good++;

				/* counts sub-blocks done */
				blocks++;
				done = 1;
			}
		} while (!done);
	} while (s + boff + (cnt * BITSINABYTE) < blk[row]->p3 - BITSINABYTE);

	sprintf(lin, "\n - Verified sub-block checkbytes : %d of %d", good, blocks);
	strcat(info, lin);

	/* In case of multiple checksums in a file, use counts instead of a checksum pair */
	/* Suggestion: for those loaders (like this one) where there's a header
	   checksum too, use it here (but don't print it to the "info" buffer) */
	blk[row]->cs_exp = blocks;
	blk[row]->cs_act = good;

	/* Decode all sub-blocks as one prg */
	rd_err = 0;

	s = blk[row]->p2;

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);

	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

	for (i = 0, tot = 0; tot < blk[row]->cx;) {
		b = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, en);
		if (b != -1) {
			blk[row]->dd[tot] = b;
		} else {
			blk[row]->dd[tot] = 0x69;	/* sentinel error value */
			rd_err++;

			/* for experts only */
			sprintf(lin, "\n - Read Error on byte @$%X (prg data offset: $%04X)", s + (i * BITSINABYTE), tot + 2);
			strcat(info, lin);
		}
		tot++;
		i++;

		if (i == SBLOCKSIZE) {
			i = 0;
			s += ((SBLOCKSIZE + 1) * BITSINABYTE); /* jump to next sub-block */
		}
	}

	blk[row]->rd_err = rd_err;

	return(rd_err);
}
