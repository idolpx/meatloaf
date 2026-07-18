/*
 * accolade.c (by Luigi Di Fraia, Aug 2006)
 * Based on cyberload_f4.c, turrican.c
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
 * Data: Sub-blocks
 * Checksum: Yes (for each sub-block)
 * Post-data: No
 * Trailer: Yes
 * Trailer homogeneous: Yes (bit 0 pulses)
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	ACCOLADE

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	1	/* amount of sync bytes */
#define MAXTRAILER	8	/* max amount of trailer pulses read in */

#define HEADERSIZE	21	/* size of block header */
#define NAMESIZE	16	/* size of filename */

#define NAMEOFFSET	0	/* filename offset inside header */
#define LOADOFFSETH	17	/* load location (MSB) offset inside header */
#define LOADOFFSETL	16	/* load location (LSB) offset inside header */
#define DATAOFFSETH	19	/* data size (MSB) offset inside header */
#define DATAOFFSETL	18	/* data size (LSB) offset inside header */
#define CHKBOFFSET	20	/* checksum offset inside header */

#define SBLOCKSIZE	256	/* sub-block size, in bytes */

void accolade_search (void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */

	int en, tp, sp, lp, sv;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x, bso; 		/* block size and its overhead due to internal checksums */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;
	sv = ft[THISLOADER].sv;

	if (!quiet)
		msgout("  Accolade (+clone)");

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

			/* Extract load location and size */
			s = hd[LOADOFFSETL] + (hd[LOADOFFSETH] << 8);
			x = hd[DATAOFFSETL] + (hd[DATAOFFSETH] << 8);

			/* Compute C64 memory location of the _LAST loaded byte_ */
			e = s + x - 1;

			/* Plausibility check */
			if (e < s || e > 0xFFFF)
				continue;

			/* Compute size overhead due to internal checksums */
			bso = x / 256;
			if (x % 256)
				bso++;

			/* Point to the first pulse of the last checkbyte (that's final) */
			/* Note: - 1 because "bso" also includes the last checkbyte! */
			eod = sod + (HEADERSIZE + x + bso - 1) * BITSINABYTE;

			/* Initially point to the last pulse of the last checkbyte */
			eof = eod + BITSINABYTE - 1;

			/* Trace 'eof' to end of trailer (bit 0 pulses only) */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) == 0)
				eof++;

			if (addblockdef(THISLOADER, sof, sod, eod, eof, 0) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int accolade_describe (int row)
{
	int i;
	int hd[HEADERSIZE];
	int en, tp, sp, lp;
	int cb;
	char bfname[NAMESIZE + 1], bfnameASCII[NAMESIZE + 1];

	int cnt, s, tot;
	int b, rd_err;
	int good, done, boff, blocks;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Set read pointer to the beginning of the payload */
	s = blk[row]->p2;

	/* Read header (it's safe to read it here for it was already decoded during the search stage) */
	for (i = 0; i < HEADERSIZE; i++)
		hd[i] = readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);

	/* Read filename */
	for (i = 0; i < NAMESIZE; i++)
		bfname[i] = hd[NAMEOFFSET + i];
	bfname[i] = '\0';
	
	trim_string(bfname);
	pet2text(bfnameASCII, bfname);

	if (blk[row]->fn != NULL)
		free(blk[row]->fn);
	blk[row]->fn = (char*)malloc(strlen(bfnameASCII) + 1);
	strcpy(blk[row]->fn, bfnameASCII);

	/* Read/compute C64 memory location for load/end address, and read data size */
	blk[row]->cs = hd[LOADOFFSETL] + (hd[LOADOFFSETH] << 8);
	blk[row]->cx = hd[DATAOFFSETL] + (hd[DATAOFFSETH] << 8);

	/* C64 memory location of the _LAST loaded byte_ */
	blk[row]->ce = blk[row]->cs + blk[row]->cx - 1;

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

	/* Test the header checkbyte */
	for (cb = 0, i = 0; i < CHKBOFFSET; i++)
		cb ^= hd[i];

	b = hd[CHKBOFFSET];

	if (cb == b) {
		sprintf(lin, "\n - Header checkbyte : OK (expected=$%02X, actual=$%02X)", b, cb);
		strcat(info, lin);
	} else {
		sprintf(lin, "\n - Header checkbyte : FAILED (expected=$%02X, actual=$%02X)", b, cb);
		strcat(info, lin);
	}

	/* Print out the number of blocks */
	sprintf(lin, "\n - Sub-blocks : %d", blocks);
	strcat(info, lin);

	/* Test all sub-block checksums individually */
	s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);
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

	s = (blk[row]->p2) + (HEADERSIZE * BITSINABYTE);

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
