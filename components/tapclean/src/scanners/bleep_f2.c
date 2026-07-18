/*
 * bleep_f2.c (rewritten by Luigi Di Fraia, Nov 2017)
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
 * Data: Continuous
 * Checksum: Yes
 * Post-data: No
 * Trailer: No
 * Trailer homogeneous: N/A
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	BLEEP_SPC

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	2	/* amount of sync bytes */
#define MAXTRAILER	2048	/* max amount of trailer pulses read in */

#define HEADERSIZE	7	/* size of block header */

#define BLKNUMOFFSET	0	/* block number offset inside header */
#define LOADOFFSETL	1	/* load location (LSB) offset inside header */
#define LOADOFFSETH	2	/* load location (MSB) offset inside header */
#define ENDOFFSETL	3	/* end  location (LSB) offset inside header */
#define ENDOFFSETH	4	/* end  location (MSB) offset inside header */
#define EXECOFFSETL	5	/* execution address (LSB) offset inside header */
#define EXECOFFSETH	6	/* execution address (MSB) offset inside header */

static int bleep_spc_search_core(int first_sof, int pv)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int last_eof;
	int hd[HEADERSIZE];		/* buffer to store block header info */

	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int sypat[SYNCSEQSIZE];		/* expected sync pattern */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	ft[THISLOADER].pv = pv;
	ft[THISLOADER].sv = pv ^ 0xFF;
	ft[THISLOADER].pmin = 5;
	ft[THISLOADER].pmax = NA;

	sypat[0] = pv ^ 0xFF;
	sypat[1] = pv;

	for (last_eof = first_sof, i = first_sof; i > 0 && i < tap.len - BITSINABYTE; i++) {
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

			/* Extract block load and chain end locations */
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

			/* Set size (remember e is the end address of the whole chain) */
			x = 0x100;

			/* Point to the first pulse of the checkbyte (that's final) */
			eod = sod + (HEADERSIZE + x) * BITSINABYTE;

			/* Initially point to the last pulse of the checkbyte */
			eof = eod + BITSINABYTE - 1;

			/* Include pilot tone if this is the first block in a chain */
			if (hd[BLKNUMOFFSET] == 0)
				sof = first_sof;

			/* Include trailer if this is the last block */
			if (e - s == 0x00FF) {
				/* Trace 'eof' to end of trailer (bit 1 pulses only) */
				h = 0;
				while (eof < tap.len - 1 &&
						h++ < MAXTRAILER &&
						readttbit(eof + 1, lp, sp, tp) == 1)
					eof++;
			}

			if (addblockdef(THISLOADER, sof, sod, eod, eof, pv) >= 0) {
				i = eof;	/* Search for further files starting from the end of this one */
				last_eof = eof;
			}

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}

	return last_eof;
}

static int bleep_spc_find_first_pilot_byte(int eop, int *pv)
{
	int i, j;

	int en, tp, sp, lp, sv;		/* encoding parameters */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* In case the pv has any of the MSbits set, we need to include some bit 1 pulses too */
	for (j = eop - 7; j <= eop; j++) {
		i = j;

		*pv = readttbyte(i, lp, sp, tp, en);

		/* We want a non-zero pilot byte */
		if (*pv <= 0)
			continue;

		do {
			i += BITSINABYTE;
		} while (readttbyte(i, lp, sp, tp, en) == *pv);
				
		sv = readttbyte(i, lp, sp, tp, en);

		/* Followed by a sync byte = pilot byte ^ 0xFF */
		if (sv < 0 || sv != (*pv ^ 0xFF))
			continue;

		i += BITSINABYTE;

		/* Followed by another sync byte = pilot byte */
		if (readttbyte(i, lp, sp, tp, en) != *pv)
			continue;

		i += BITSINABYTE;

		/* As there's a pilot tone, this must be the first block, #0 */
		if (readttbyte(i, lp, sp, tp, en) != 0x00)
			continue;

		return i;
	}

	return 0;
}

void bleep_spc_search(void)
{
	int i;				/* counter */
	int sof, sod, eof, eop;		/* file offsets */

	int pv;				/* encoding parameters */

	if (!quiet)
		msgout("  Bleepload Special");

	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {
		ft[THISLOADER].pv = 1;
		ft[THISLOADER].sv = 0;
		ft[THISLOADER].pmin = 512;
		ft[THISLOADER].pmax = NA;

		eop = find_pilot(i, THISLOADER);

		if (eop > 0) {
			sod = bleep_spc_find_first_pilot_byte(eop, &pv);
			
			if (!sod)
				continue;

			/* Valid pilot found, mark start of file */
			sof = i;
			i = sod;

			//printf("\nMight have found Bleepload Special @$%X, pv=$%02X", sof, pv);

			eof = bleep_spc_search_core(sof, pv);

			if (eof > i)
				i = eof;

			//printf("\nResuming search @$%X", i);

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int bleep_spc_describe(int row)
{
	int i, s;
	int hd[HEADERSIZE];
	int en, tp, sp, lp;
	int cb;

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
	blk[row]->ce = blk[row]->cs + 0x00FF;

	/* Set size */
	blk[row]->cx = 0x100;

	/* Compute pilot & trailer lengths */

	/* pilot is in bytes... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / BITSINABYTE;

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync sequence */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= SYNCSEQSIZE;

	/* Print pilot byte */
	sprintf(lin, "\n - Pilot byte : $%02X", blk[row]->xi);
	strcat(info, lin);

	/* Extract block ID, chain end and execution address and print these out */
	sprintf(lin, "\n - Block Number : $%02X", hd[BLKNUMOFFSET]);
	strcat(info, lin);
	sprintf(lin, "\n - Chain end address : $%04X", hd[ENDOFFSETL] + (hd[ENDOFFSETH] << 8) - 1);
	strcat(info, lin);
	sprintf(lin, "\n - Exe Address : $%04X", hd[EXECOFFSETL] + (hd[EXECOFFSETH] << 8));
	strcat(info, lin);

	/* Extract data and test checksum... */
	rd_err = 0;
	cb = 0;

	s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);

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
