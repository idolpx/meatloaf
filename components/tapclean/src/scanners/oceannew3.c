/*
 * oceannew3.c (by Luigi Di Fraia, Jan 2013)
 * Based on oceannew4.c
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
 * Note: Do not copy code from this scanner because it uses a convenience lp
 *       value for pilot pulses only; mp and sp are used for data bits.
 *
 * CBM inspection needed: No
 * Single on tape: No
 * Sync: Byte
 * Header: Yes
 * Data: Sub-blocks
 * Checksum: Yes (inside header)
 * Post-data: No
 * Trailer: Spike
 * Trailer homogeneous: N/A
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	OCNEW3

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define MAXTRAILER	8	/* max amount of trailer pulses read in */

#define HEADERSIZE	4	/* size of block header */

#define BLKNUMOFFSET	1	/* block number offset inside header */
#define CHKBYOFFSET	3	/* chekcbyte offset inside header */


void oceannew3_search (void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */

	int en, tp, sp, mp, lp, sv;	/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size and its overhead due to padding */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	mp = ft[THISLOADER].mp;
	lp = ft[THISLOADER].lp;
	sv = ft[THISLOADER].sv;

	if (!quiet)
		msgout("  New Ocean Tape 3");

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

			/* Valid sync found, mark start of data */
			sod = i;

			/* Read header */
			for (h = 0; h < HEADERSIZE; h++) {
				hd[h] = readttbyte(sod + h * BITSINABYTE, mp, sp, tp, en);
				if (hd[h] == -1)
					break;
			}

			/* Bail out if there was an error reading the block header */
			if (h != HEADERSIZE)
				continue;

			/* Set load and end locations (work in progress) */
			s = 0x0D00 + 256 * hd[BLKNUMOFFSET];
			e = s + 0x0100;

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

			/* Point to the last pulse of the last byte */
			eof = eod + BITSINABYTE - 1;

			if (addblockdef(THISLOADER, sof, sod, eod, eof, 0) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int oceannew3_describe (int row)
{
	int i, s;
	int hd[HEADERSIZE];
	int en, tp, sp, mp;
	int cb;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	mp = ft[THISLOADER].mp;

	/* Set read pointer to the beginning of the payload */
	s = blk[row]->p2;

	/* Read header (it's safe to read it here for it was already decoded during the search stage) */
	for (i = 0; i < HEADERSIZE; i++)
		hd[i] = readttbyte(s + i * BITSINABYTE, mp, sp, tp, en);

	sprintf(lin, "\n - Block Number : $%02X", hd[BLKNUMOFFSET]);
	strcat(info, lin);

	/* Extract load and end locations */
	blk[row]->cs = 0x0D00 + 256 * hd[BLKNUMOFFSET];
	blk[row]->ce = blk[row]->cs + 0x0100;

	/* Prevent int wraparound when subtracting 1 from end location
	   to get the location of the last loaded byte */
	if (blk[row]->ce == 0)
		blk[row]->ce = 0xFFFF;
	else
		(blk[row]->ce)--;

	/* Compute size (no padding, just extract the advertised amount of bytes) */
	blk[row]->cx = blk[row]->ce - blk[row]->cs + 1;

	/* Compute pilot & trailer lengths */

	/* pilot is in pulses... */
	blk[row]->pilot_len = blk[row]->p2 - blk[row]->p1;

	/* ... trailer in pulses */
	/* Note: No trailer has been documented, but we are not pretending it
	         here, just checking for it is future-proof */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync sequence */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= 1;

	/* Extract data and test checksum... */
	rd_err = 0;
	cb = 0;

	s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);

	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

	for (i = 0; i < blk[row]->cx; i++) {
		b = readttbyte(s + (i * BITSINABYTE), mp, sp, tp, en);
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

	b = hd[CHKBYOFFSET];

	blk[row]->cs_exp = cb & 0xFF;
	blk[row]->cs_act = b  & 0xFF;
	blk[row]->rd_err = rd_err;

	return(rd_err);
}
