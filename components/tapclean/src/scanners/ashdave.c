/*
 * ashdave.c (by Luigi Di Fraia, May 2008)
 * Based on burner.c
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
 * Sync: Byte
 * Header: Yes
 * Data: Continuous
 * Checksum: No
 * Post-data: Yes
 * Trailer: Yes
 * Trailer homogeneous: No
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	ASHDAVE

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	1	/* amount of sync bytes */
#define MAXTRAILER	16	/* max amount of trailer pulses read in */

#define HEADERSIZE	5	/* size of block header */

#define BLKNUMOFFSET	0	/* block number offset inside header */
#define LOADOFFSETH	2	/* load location (MSB) offset inside header */
#define LOADOFFSETL	1	/* load location (LSB) offset inside header */
#define ENDOFFSETH	4	/* end  location (MSB) offset inside header */
#define ENDOFFSETL	3	/* end  location (LSB) offset inside header */

#define POSTDATASIZE	15	/* size in bytes of the MANDATORY information
				   that is found after file data */

/* If defined, the postdata pattern is not mandatory and its content is
displayed in the report */
//#define DEBUGASHDAVE

void ashdave_search (void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */
#ifndef DEBUGASHDAVE
	int pat[POSTDATASIZE];		/* buffer to store postdata pattern */
#endif

	int en, tp, sp, lp, sv;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int xinfo;			/* extra info used in addblockdef() */

#ifndef DEBUGASHDAVE
	/* legacy postdata pattern (at least the one that is not corrupted) */
	static int sypat[POSTDATASIZE] = {
		0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
		0x11, 0x22, 0x33, 0x44, 0x55, 0x66
	};
	int match;			/* condition variable */
#endif


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;
	sv = ft[THISLOADER].sv;

	if (!quiet)
		msgout("  Ash&Dave");

	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {
		eop = find_pilot(i, THISLOADER);

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

#ifndef DEBUGASHDAVE
			/* Decode a POSTDATASIZE byte sequence (possibly a valid postdata pattern) */
			for (h = 0; h < POSTDATASIZE; h++)
				pat[h] = readttbyte(eof + (h * BITSINABYTE), lp, sp, tp, en);

			/* Note: no need to check if readttbyte is returning -1, for
			         the following comparison (DONE ON ALL READ BYTES)
			         will fail all the same in that case */

			/* Check postdata pattern. We may use the find_seq() facility too */
			for (match = 1, h = 0; h < POSTDATASIZE; h++)
				if (pat[h] != sypat[h])
					match = 0;

			/* Postdata pattern doesn't match */
			if (!match)
				continue;

			/* Point to the last pulse of the last postdata byte */
			eof = eod + (POSTDATASIZE + 1) * BITSINABYTE - 1;
#endif

			/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) >= 0)
				eof++;

			/* Reconstruction of the last pulse of the last postdata byte (0x77) */
			/* Note: this should eventually be moved to the cleaning stage */
			xinfo = 0;
			if (eof - (eod + (POSTDATASIZE + 1) * BITSINABYTE - 1) == BITSINABYTE - 1) {
				if (eof < tap.len - 1 && tap.tmem[eof + 1] != 0x00) {
					xinfo = 1;
					tap.tmem[eof + 1] = lp;
					eof++;
				}
			}

			if (addblockdef(THISLOADER, sof, sod, eod, eof, xinfo) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int ashdave_describe (int row)
{
	int i, s;
	int hd[HEADERSIZE];
	int en, tp, sp, lp;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Set read pointer to the beginning of the payload */
	s = blk[row]->p2;

	/* Read header (it's safe to read it here for it was already decoded during the search stage) */
	for (i = 0; i < HEADERSIZE; i++)
		hd[i] = readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);

	sprintf(lin, "\n - Block ID : $%02X", hd[BLKNUMOFFSET]);
	strcat(info, lin);

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

#ifdef DEBUGASHDAVE
	/* Postdata pattern */
	strcat(info, "\n - Postdata : ");
	for (; i < blk[row]->cx + POSTDATASIZE; i++) {
		b = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, en);

		// Do NOT increase read errors for this one is not within DATA
		if (b == -1)
			strcat(info, "-- ");
		else {
			sprintf(lin, "%02X ", b);
			strcat(info, lin);
		}
	}
#endif

	if (blk[row]->xi) {
		strcat(info, "\n - Automatically fixed last trailer byte ");
		strcat(info, "\n   to be accounted by the cleaning process");
	}

	blk[row]->rd_err = rd_err;

	return(rd_err);
}
