/*
 * mms.c (by Luigi Di Fraia, Dec 2018)
 * Based on biturbo.c
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
 * Single on tape: No! -> requires tracking of the right CBM file (done)
 * Sync: Sequence (bytes)
 * Header: Yes
 * Data: Continuous
 * Checksum: Yes
 * Post-data: No
 * Trailer: Yes
 * Trailer homogeneous: Yes (bit 0 pulses)
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	MMS

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	8	/* amount of sync bytes */
#define MAXTRAILER	2040	/* max amount of trailer pulses read in */

#define LOADOFFSETH	0x71	/* load location (MSB) offset inside CBM header */
#define LOADOFFSETL	0x6B	/* load location (LSB) offset inside CBM header */
#define ENDOFFSETH	0x7B	/* end location (MSB) offset inside CBM header */
#define ENDOFFSETL	0x77	/* end location (LSB) offset inside CBM header */

#define MAXCBMBACKTRACE	0x3980  /* max amount of pulses between a turbo file and the
				   'REPEATED' instance of its CBM header block.
				   The typical value is less than this one */

void mms_search (void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int ib;				/* condition variable */

	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	/* Expected sync pattern */
	static int sypat[SYNCSEQSIZE] = {
		0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00
	};

	int match;			/* condition variable */
	int cbm_index;			/* Index of the CBM Header block to get info from */

	int xinfo;			/* extra info used in addblockdef() */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	if (!quiet)
		msgout("  MMS Tape");

	cbm_index = 1;

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

			/* Valid sync train found, mark start of data */
			sod = i + SYNCSEQSIZE * BITSINABYTE;

			/* Now we try to retrieve MMS variables from the corresponding CBM
			   Header block ('REPEATED' instance).
			   We search for the CBM Header block whose start offset in the TAP file
			   is not too far from where we found the actual MMS file */

			match = 1;

			for (;; cbm_index++) {
				ib = find_decode_block(CBM_HEAD, cbm_index);
				if (ib == -1)
					return;		/* failed to locate CBM Header for this one and any further MMS file. */

				/*
				printf("\nCBM Header (repeated) possibly at %x for block with sof = %x (diff = %x)", 
				        blk[ib]->p1, 
					sof, 
					sof - blk[ib]->p1);
				*/

				/* Plausibility checks. Here since we track the CBM part for each
				   of them, in case of multiple MMS files on the same tape:
				   there may be some programs using MMS, some others using another loader,
				   so that the n-th MMS file may not come just after the n-th CBM file. */
				if (blk[ib]->p1 < sof - MAXCBMBACKTRACE)
					continue;	/* Not yet the right CBM Header block */

				if (blk[ib]->p1 > sof) {
					match = 0;	/* Too far ahead: failed to locate CBM Header for this MMS file only. */
					cbm_index--;	/* Make the last checked CBM Header instance available to the following MMS files, if any */
					break;
				}

				/* Basic validation before accessing array elements */
				if (blk[ib]->cx < ENDOFFSETH + 1)
					continue;

				/* Extract load and end locations */
				s = blk[ib]->dd[LOADOFFSETL] + (blk[ib]->dd[LOADOFFSETH] << 8);
				e = blk[ib]->dd[ENDOFFSETL] + (blk[ib]->dd[ENDOFFSETH] << 8);

				/* Prevent int wraparound when subtracting 1 from end location
				   to get the location of the last loaded byte */
				if (e == 0)
					e = 0xFFFF;
				else
					e--;

				/* Plausibility check. Maybe a read error in the 'FIRST' instance of CBM Header, so it's
				   worth trying the next CBM Header file, which should be the 'REPEATED' instance. */
				if (e < s)
					continue;

				/* Move past this one as it's being used */
				cbm_index++;

				break;
			}

			/* Failed to find the CBM Header block for this MMS file (maybe CBM part is unrecognized) */
			if (!match)
				continue;

			/* Compute size */
			x = e - s + 1;

			/* Point to the first pulse of the checkbyte (that's final) */
			eod = sod + x * BITSINABYTE;

			/* Initially point to the last pulse of the checkbyte */
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
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int mms_describe(int row)
{
	int i, s;
	int en, tp, sp, lp;
	int cb;

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

	/* pilot is in bytes... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / BITSINABYTE;

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync sequence */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= SYNCSEQSIZE;

	/* Extract data and test checksum... */
	rd_err = 0;
	cb = 0;

	s = blk[row]->p2;

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);

	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

	for (i = 0; i < blk[row]->cx; i++) {
		b = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, en);
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

	b = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, en);
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
