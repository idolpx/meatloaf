/*
 * amaction.c (by Luigi Di Fraia, March 2012)
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
 * Note: Do not copy code from this scanner because it implements a filename
 *       size calculation based on loader-specific constants.
 *
 * CBM inspection needed: No
 * Single on tape: No
 * Sync: Sequence (bytes)
 * Header: Yes
 * Data: Continuous
 * Checksum: No
 * Post-data: No
 * Trailer: No
 * Trailer homogeneous: N/A
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	AMACTION

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	9	/* amount of sync bytes */
#define MAXTRAILER	8	/* max amount of trailer pulses read in */

#define HEADERSIZE	5	/* size of block header */
#define MAXFILENAME	16	/* estimated max filename length */

#define NAMEOFFSET	1	/* filename offset inside header */
#define LOADOFFSETH	3	/* load location (MSB) offset inside header (excluding filename) */
#define LOADOFFSETL	4	/* load location (LSB) offset inside header (excluding filename) */
#define DATAOFFSETH	1	/* data size-1 (MSB) offset inside header (excluding filename) */
#define DATAOFFSETL	2	/* data size-1 (LSB) offset inside header (excluding filename) */

void amaction_search (void)
{
	int i, h;			/* counter */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE + MAXFILENAME];	/* buffer to store block header info */

	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	static int s_default[2] = {	/* default load address for most files */
		0x08, 0x01
	};
	static int s_pointx[2] = {	/* load address for one of Point X files */
		0x08, 0x20
	};

	int xinfo;			/* extra info used in addblockdef() */

	/* Expected sync pattern */
	static int sypat[SYNCSEQSIZE] = {
		0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01
	};


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	if (!quiet)
		msgout("  American Action tape");

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

			/* Read header */
			for (h = 0; h < HEADERSIZE + MAXFILENAME; h++) {
				hd[h] = readttbyte(sod + h * BITSINABYTE, lp, sp, tp, en);
				if (hd[h] == -1)
					break;
			}
			if (h != HEADERSIZE + MAXFILENAME)
				continue;

			/* Figure out filename length */
			xinfo = find_seq(hd, HEADERSIZE + MAXFILENAME, s_default, sizeof(s_default) / sizeof(s_default[0]));
			if (xinfo == -1) {
				xinfo = find_seq(hd, HEADERSIZE + MAXFILENAME, s_pointx, sizeof(s_pointx) / sizeof(s_pointx[0]));
				if (xinfo == -1) {
					xinfo = 2;
				} else {
					/* Remove data size and flag from count */
					xinfo -= 3;
				}	
			} else {
				/* Remove data size and flag from count */
				xinfo -= 3;
			}

			/* Plausibility check */
			if (xinfo < 0)
				continue;

			/* Extract load location and size */
			s = hd[LOADOFFSETL + xinfo] + (hd[LOADOFFSETH + xinfo] << 8);
			x = hd[DATAOFFSETL + xinfo] + (hd[DATAOFFSETH + xinfo] << 8) + 1;

			/* Compute C64 memory location of the _LAST loaded byte_ */
			e = s + x - 1;

			/* Plausibility check */
			if (e > 0xFFFF)
				continue;

			/* Point to the first pulse of the last data byte (that's final) */
			eod = sod + (HEADERSIZE + xinfo + x - 1) * BITSINABYTE;

			/* Point to the last pulse of the last byte */
			eof = eod + BITSINABYTE - 1;

			/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
			/* Note: No trailer has been documented, but we are not strictly
			         requiring one here, just checking for it is future-proof */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) >= 0)
				eof++;

			if (addblockdef(THISLOADER, sof, sod, eod, eof, xinfo) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int amaction_describe (int row)
{
	int i, s;
	int hd[HEADERSIZE + MAXFILENAME], fname;
	int en, tp, sp, lp;
	char bfname[MAXFILENAME + 1], bfnameASCII[MAXFILENAME + 1];

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Set read pointer to the beginning of the payload */
	s = blk[row]->p2;

	fname = blk[row]->xi;

	/* Read header (it's safe to read it here for it was already decoded during the search stage) */
	for (i = 0; i < HEADERSIZE + fname; i++)
		hd[i] = readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);

	/* Copy filename */
	for (i = 0; i < fname; i++)
		bfname[i] = hd[NAMEOFFSET + i];
	bfname[i] = '\0';

	trim_string(bfname);
	pet2text(bfnameASCII, bfname);

	if (blk[row]->fn != NULL)
		free(blk[row]->fn);
	blk[row]->fn = (char*)malloc(strlen(bfnameASCII) + 1);
	strcpy(blk[row]->fn, bfnameASCII);

	/* Read/compute C64 memory location for load/end address, and read data size */
	blk[row]->cs = hd[LOADOFFSETL + fname] + (hd[LOADOFFSETH + fname] << 8);
	blk[row]->cx = hd[DATAOFFSETL + fname] + (hd[DATAOFFSETH + fname] << 8) + 1;

	/* C64 memory location of the _LAST loaded byte_ */
	blk[row]->ce = blk[row]->cs + blk[row]->cx - 1;

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

	/* Extract data */
	rd_err = 0;

	s = blk[row]->p2 + ((HEADERSIZE + fname) * BITSINABYTE);

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
