/*
 * tequila.c (by Luigi Di Fraia, Feb 2010)
 * Takes its name from the text "TEQUILA SUNRISE BELGIUM BRUSSELS  1984"
 * Based on anirog.c
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
 * Note: Do not copy code from this scanner because it implements a reader for
 *       mixed MSbF and LSbF data, as found in this format.
 *
 * CBM inspection needed: No
 * Single on tape: No
 * Sync: Sequence (bytes)
 * Header: Yes
 * Data: Continuous
 * Checksum: No
 * Post-data: Yes
 * Trailer: No
 * Trailer homogeneous: N/A
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	TEQUILA

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	9	/* amount of sync bytes */
#define MAXTRAILER	8	/* max amount of trailer pulses read in */

#define HEADERSIZE	5	/* size of block header */

#define BLKNUMOFFSET	0	/* block number offset inside header */
#define LOADOFFSETH	2	/* load location (MSB) offset inside header */
#define LOADOFFSETL	1	/* load location (LSB) offset inside header */
#define ENDOFFSETH	4	/* end  location (MSB) offset inside header */
#define ENDOFFSETL	3	/* end  location (LSB) offset inside header */

#define POSTDATASIZE	2	/* size in bytes of the MANDATORY information
				   that is found after file data */

void tequila_search(void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */

	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	/* Expected sync pattern */
	static int sypat[SYNCSEQSIZE] = {
		0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01
	};


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	if (!quiet)
		msgout("  Tequila Sunrise");

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

			/* Read header (force LSbF for this variant) */
			for (h = 0; h < HEADERSIZE; h++) {
				hd[h] = readttbyte(sod + h * BITSINABYTE, lp, sp, tp, LSbF);
				if (hd[h] == -1)
					break;
			}

			/* Bail out if there was an error reading the block header */
			if (h != HEADERSIZE)
				continue;

			/* Extract load and end locations */
			s = hd[LOADOFFSETL] + (hd[LOADOFFSETH] << 8);
			e = hd[ENDOFFSETL]  + (hd[ENDOFFSETH]  << 8);

			/*
			 * Be aware of blocks that load up to top of RAM and back to the beginning.
			 *
			 * FE00-0002 blocks have been found in Bongo, Strip Poker II, and Zodiac.
			 * Contents change, but these all contain the following text:
			 *   "XROM SYSTEM TEQUILA SUNRISE BELGIUM BRUSSELS  1984"
			 */
			if (s == 0xFE00 && e == 0x0002)
				e += 0x10000;

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

			/* Point to the first pulse of the exe ptr, MSB */
			eod = sod + (HEADERSIZE + x + POSTDATASIZE - 1) * BITSINABYTE;

			/* Initially point to the last pulse of the exe ptr MSB */
			eof = eod + BITSINABYTE - 1;

			/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
			/* Note: No trailer has been documented, but we are not strictly
			         requiring one here, just checking for it is future-proof */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) >= 0)
				eof++;

			if (addblockdef(THISLOADER, sof, sod, eod, eof, 0) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int tequila_describe(int row)
{
	int i, s;
	int hd[HEADERSIZE], hd_id;
	int pd[POSTDATASIZE];
	int en, tp, sp, lp;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Set read pointer to the beginning of the payload */
	s = blk[row]->p2;

	/* Read header (Force LSbF) (it's safe to read it here for it was already decoded during the search stage) */
	for (i = 0; i < HEADERSIZE; i++)
		hd[i] = readttbyte(s + i * BITSINABYTE, lp, sp, tp, LSbF);

	/* Endianness is MSbF up to File ID so we decode File ID on its own */
	hd_id = readttbyte(s + BLKNUMOFFSET * BITSINABYTE, lp, sp, tp, en);
	sprintf(lin, "\n - Block Number : $%02X", hd_id);
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

	/* Compute size (be aware of blocks that load up to top of RAM and back to the beginning) */
	if (blk[row]->ce < blk[row]->cs)
		blk[row]->cx = blk[row]->ce + 0x10000 - blk[row]->cs + 1;
	else
		blk[row]->cx = blk[row]->ce - blk[row]->cs + 1;

	/* Compute pilot & trailer lengths */

	/* pilot is in bytes... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / BITSINABYTE;

	/* ... trailer in pulses */
	/* Note: No trailer has been documented, but we are not pretending it
	         here, just checking for it is future-proof */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync sequence */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= SYNCSEQSIZE;

	/* Extract data (Force LSbF) */
	rd_err = 0;

	s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);

	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

	for (i = 0; i < blk[row]->cx; i++) {
		b = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, LSbF);
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

	/* Read post-data */
	for (i = 0; i < POSTDATASIZE; i++) {
		b = readttbyte(s + (blk[row]->cx + i) * BITSINABYTE, lp, sp, tp, LSbF);

		/* Increase read errors for this data is mandatory for the correct execution of the software */
		if (b != -1) {
			pd[i] = b;
		} else {
			rd_err++;

			/* for experts only */
			sprintf(lin, "\n - Read Error on post-data byte @$%X (offset: $%02X)", s + (blk[row]->cx + i) * BITSINABYTE, i);
			strcat(info, lin);

			break;
		}
	}

	/* Print execution ptr only if it was read in properly */
	strcat(info, "\n - Post-data Ptr ");
	sprintf(lin, "@$%X : ", s + blk[row]->cx * BITSINABYTE);
	strcat(info, lin);

	if (i == POSTDATASIZE) {
		sprintf(lin, "$%04X", (pd[0]<<8) + pd[1] + 1);
		strcat(info, lin);
	} else {
		strcat(info, "$----");
	}


	blk[row]->rd_err = rd_err;

	return(rd_err);
}

