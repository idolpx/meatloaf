/*
 * freezemachine.c (rewritten from scratch by Luigi Di Fraia, Aug 2006)
 *
 * Part of project "TAPClean". May be used in conjunction with "Final TAP".
 *
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
 * Sync: Bit + Byte
 * Header: No
 * Data: Continuous
 * Checksum: No
 * Post-data: No
 * Trailer: Yes
 * Trailer homogeneous: Yes (bit 0 pulses) + 1 bit 1
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	FREEZEMACHINE

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define EXTRASYNCVAL	0xAA	/* value of the extra sync byte */
#define MAXTRAILER	2032	/* max amount of trailer pulses read in */

#define LOADOFFSETH	0x0E	/* load location (MSB) offset inside CBM data */
#define LOADOFFSETL	0x0A	/* load location (LSB) offset inside CBM data */
#define ENDOFFSETH	0x1F	/* end location (MSB) offset inside CBM header */
#define ENDOFFSETL	0x1B	/* end location (LSB) offset inside CBM header */

void freezemachine_search (void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int ib;				/* condition variable */

	int en, tp, sp, lp, sv;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int xinfo;			/* extra info used in addblockdef() */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;
	sv = ft[THISLOADER].sv;

	if (!quiet)
		msgout("  Freeze Machine tape");

	/*
	 * First we retrieve loader variables from the CBM header and data.
	 * We use CBM DATA index # 1 as we assume the tape image contains 
	 * a single game.
	 * For compilations we should search and find the relevant file 
	 * using the search code found e.g. in Biturbo.
	 */
	ib = find_decode_block(CBM_DATA, 1);
	if (ib == -1)
		return;		/* failed to locate CBM data. */

	/* Basic validation before accessing array elements */
	if (blk[ib]->cx < LOADOFFSETH + 1)
		return;

	s = blk[ib]->dd[LOADOFFSETL] + (blk[ib]->dd[LOADOFFSETH] << 8); /* 0x0801 */

	/*
	 * We use CBM HEAD index # 1 as we assume the tape image contains 
	 * a single game.
	 * For compilations we should search and find the relevant file 
	 * using the search code found e.g. in Biturbo.
	 */
	ib = find_decode_block(CBM_HEAD, 1);
	if (ib == -1)
		return;		/* failed to locate cbm header. */

	/* Basic validation before accessing array elements */
	if (blk[ib]->cx < ENDOFFSETH + 1)
		return;

	e = blk[ib]->dd[ENDOFFSETL] + (blk[ib]->dd[ENDOFFSETH] << 8);

	/* Prevent int wraparound when subtracting 1 from end location
	   to get the location of the last loaded byte */
	if (e == 0)
		e = 0xFFFF;
	else
		e--;

	/* Plausibility checks (here since FM is always just ONE TURBO file) */
	/* Note: a plausibility check is on s == 0x0801 because load address
		 is stored in CBM data, which is the very same file for all
		 genuine Freeze Machine tapes! */
	if (e < s || s != 0x0801)
		return;

	/* Note: we may exit the "for" cycle if addblockdef() doesn't fail,
	   since FM is always just ONE turbo file. I didn't do that because
	   we may have more than one game on the same tape using FM loader,
	   but such in a case whe must retrieve CBM information from their
	   respective CBM parts... Not done actually. */
	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {
		eop = find_pilot(i, THISLOADER);

		if (eop > 0) {
			/* Valid pilot found, mark start of file */
			sof = i;
			i = eop;

			/* Check if there's a valid sync bit for this loader */
			if (readttbit(i, lp, sp, tp) != sv)
				continue;

			i++; /* Take into account this bit */

			/* Check if there's a valid extra sync byte for this loader */
			if (readttbyte(i, lp, sp, tp, en) != EXTRASYNCVAL)
				continue;

			/* Valid sync bit + sync byte found, mark start of data */
			sod = i + BITSINABYTE;

			/* Compute size */
			x = e - s + 1;

			/* Point to the first pulse of the last data byte (that's final) */
			eod = sod + (x - 1) * BITSINABYTE;

			/* Point to the last pulse of the last byte */
			eof = eod + BITSINABYTE - 1;

			/* Trace 'eof' to end of trailer (bit 0 pulses only) */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) == 0)
				eof++;

			/* Also account the single bit 1 trailer pulse, if any */
			if (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) == 1)
				eof++;

			/* Store the info read from CBM part as extra-info */
			xinfo = s + (e << 16);

			if (addblockdef(THISLOADER, sof, sod, eod, eof, xinfo) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */
						/* Note: there's no further file in a legacy FM tape,
						         so we should just "return" here. */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int freezemachine_describe(int row)
{
	int i, s;
	int en, tp, sp, lp;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Retrieve C64 memory location for load/end address from extra-info */
	blk[row]->cs = blk[row]->xi & 0xFFFF;
	blk[row]->ce = (blk[row]->xi & 0xFFFF0000) >> 16;
	blk[row]->cx = (blk[row]->ce - blk[row]->cs) + 1;

	/* Compute pilot & trailer lengths */

	/* pilot is in pulses... */
	blk[row]->pilot_len = blk[row]->p2 - blk[row]->p1;

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync sequence (9 bits) */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= 1 + BITSINABYTE;

	/* Extract data */
	rd_err = 0;

	s = blk[row]->p2;

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
