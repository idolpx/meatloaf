/*
 * wildload.c (rewritten by Luigi Di Fraia, Jul 2017)
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
 * Sync: Sequence (bytes)
 * Header: Yes
 * Data: Continuous (loaded backwards)
 * Checksum: Yes (XOR)
 * Post-data: No
 * Trailer: No
 * Trailer homogeneous: N/A
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	WILD

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	10	/* amount of sync bytes */
#define MAXTRAILER	8	/* max amount of trailer pulses read in */

#define HEADERSIZE	5	/* size of block header */

#define ENDOFFSETH	1	/* end  location (MSB) offset inside header */
#define ENDOFFSETL	0	/* end  location (LSB) offset inside header */
#define DATAOFFSETH	3	/* data size (MSB) offset inside header */
#define DATAOFFSETL	2	/* data size (LSB) offset inside header */

void wild_search(void)
{
	int i, h;			/* counter */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */

	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int e;			/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	/* Expected sync pattern */
	static int sypat[SYNCSEQSIZE] = {
		0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01
	};


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	if (!quiet)
		msgout("  Wildload");
         
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
			for (h = 0; h < HEADERSIZE; h++) {
				hd[h] = readttbyte(sod + h * BITSINABYTE, lp, sp, tp, en);
				if (hd[h] == -1)
					break;
			}

			/* Bail out if there was an error reading the block header */
			if (h != HEADERSIZE)
				continue;

			/* Extract end location (the first byte is stored here) and size */
			e = hd[ENDOFFSETL]  + (hd[ENDOFFSETH]  << 8);
			x = hd[DATAOFFSETL] + (hd[DATAOFFSETH] << 8);

			/* Plausibility check */
			if (e + 1 < x)
				continue;

			/* Point to the first pulse of the checkbyte (that's final) */
			eod = sod + (HEADERSIZE + x) * BITSINABYTE;

			/* Initially point to the last pulse of the checkbyte */
			eof = eod + BITSINABYTE - 1;

			/* Add 1st block */
			if (addblockdef(THISLOADER, sof, sod, eod, eof, 0) < 0)
				continue;

			/* Locate chained blocks */

			sof = eof + 1;	/* Set pointer to next possible block in chain */

			do {
				/* Find data length for this block */
				for (h = 0; h < HEADERSIZE; h++) {
					hd[h] = readttbyte(sof + h * BITSINABYTE, lp, sp, tp, en);
					if (hd[h] == -1)
						break;        /* Read error in header: abort */
				}

				if (h < HEADERSIZE)
					break;

				x = hd[DATAOFFSETL] + (hd[DATAOFFSETH] << 8);

				/* If length read is $0000 then we're finished */
				if (x) {
					sod = sof;
					eod = sod + (HEADERSIZE + x) * BITSINABYTE;
					eof = eod + BITSINABYTE - 1;

					if (addblockdef(THISLOADER, sof, sod, eod, eof, 0) < 0)
						break;

					sof = eof + 1;	/* Bump pointer */
				} else {
					sod = sof;
					eod = sod + HEADERSIZE * BITSINABYTE;
					eof = eod + BITSINABYTE - 2;	/* Size is always 47 pulses */

					if (addblockdef(WILD_STOP, sof, sod, eod, eof, 0) < 0)
						break;

					sof = eof + 1;	/* Bump pointer */
				}

			} while (x && sof < tap.len);

			i = sof;	/* Make sure following headblock search starts at correct position */
		}
	}
}

int wild_describe(int row)
{
	int i;
	int hd[HEADERSIZE];
	int en, tp, sp, lp;

	int s, len, scnt;
	int b, rd_err;
	int cb;


	/* Same encoding parameters for WILD and WILD_STOP */
	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	rd_err = 0;

	if (blk[row]->lt == WILD) {

		/* Set read pointer to the beginning of the payload */
		s = blk[row]->p2;

		/* Read header (it's safe to read it here for it was already decoded during the search stage) */
		for (i = 0; i < HEADERSIZE; i++)
			hd[i] = readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);

		/* Compute/read C64 memory location for load/end address, and read data size */
		blk[row]->ce = hd[ENDOFFSETL]  + (hd[ENDOFFSETH]  << 8);	/* Note: data loads backwards */
		blk[row]->cx = hd[DATAOFFSETL] + (hd[DATAOFFSETH] << 8);
		blk[row]->cs = ((blk[row]->ce - blk[row]->cx) + 1) & 0xFFFF;

		/* Compute pilot & trailer lengths */

		/* pilot is in bytes... */
		blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / BITSINABYTE;

		/* ... trailer in pulses */
		blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

		/* if there IS pilot then disclude the sync byte */
		if (blk[row]->pilot_len > 0) 
			blk[row]->pilot_len -= SYNCSEQSIZE;

		/* Extract data (loaded backwards) and test checksum... */
		cb = 0;

		s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);

		if (blk[row]->dd != NULL)
			free(blk[row]->dd);

		blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

		scnt = blk[row]->ce & 0xFF;	/* ...end (start) address is required for correct decoding. */
		len  = blk[row]->cx;

		for (i = 0; i < blk[row]->cx; i++) {
			b = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, en);
			cb ^= (b ^ scnt);

			if (b != -1) {
				blk[row]->dd[len - 1 - i] = b ^ (scnt & 0xFF);
			} else {
				blk[row]->dd[len - 1 - i] = 0x69;	/* sentinel error value */
				rd_err++;

				/* for experts only */
				sprintf(lin, "\n - Read Error on byte @$%X (prg data offset: $%04X)", s + (i * BITSINABYTE), len - 1 - i + 2);
				strcat(info, lin);
			}

			scnt--;
		}

		b = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, en); /* read actual cb.  */
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

	} else if (blk[row]->lt == WILD_STOP) {

		sprintf(lin, "\n - Size : 47 pulses (always)");
		strcat(info, lin);

		/* Set read pointer to the beginning of the payload */
		s = blk[row]->p2;

		/* Read header (it's safe to read it here for it was already decoded during the search stage) */
		for (i = 0; i < HEADERSIZE; i++)
			hd[i] = readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);

		/* Compute/read C64 memory location for load/end address, and read data size */
		blk[row]->ce = hd[ENDOFFSETL]  + (hd[ENDOFFSETH]  << 8);	/* Note: data loads backwards */
		blk[row]->cx = hd[DATAOFFSETL] + (hd[DATAOFFSETH] << 8);
		blk[row]->cs = 0x0000;
	}

	return(rd_err);
}
