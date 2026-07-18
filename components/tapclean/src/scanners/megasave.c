/*
 * megasave.c (rewritten by Luigi Di Fraia, Sep 2017)
 *
 * Handles all known threshold types: Mega-Save Mega-Speed, Ultra-Speed, Hyper-Speed
 * (aka. Cauldron, Hewson, Rainbird), and the "slow" clone
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
#include <stdlib.h>
#include <stdio.h>

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define PREPILOTVALUE	0x20	/* value found before pilot */

#define SYNCSEQSIZE	156	/* amount of sync bytes */

#define HEADERSIZE	10	/* size of block header */

#define LOADOFFSETH	1	/* load location (MSB) offset inside header */
#define LOADOFFSETL	0	/* load location (LSB) offset inside header */
#define ENDOFFSETH	3	/* end  location (MSB) offset inside header */
#define ENDOFFSETL	2	/* end  location (LSB) offset inside header */
#define EXECOFFSETH	5	/* execution address (MSB) offset inside header */
#define EXECOFFSETL	4	/* execution address (LSB) offset inside header */

#define EXEFLAG1OFFSET	6	/* execution flag #1 offset inside header */
#define EXEFLAG2OFFSET	7	/* execution flag #2 offset inside header */

static void megasave_search_core(int lt)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */

	int en, tp, sp, lp, sv;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int xinfo;			/* extra info used in addblockdef() */


	en = ft[lt].en;
	tp = ft[lt].tp;
	sp = ft[lt].sp;
	lp = ft[lt].lp;
	sv = ft[lt].sv;

	if (!quiet) {
		sprintf(lin, "  Mega-Save T%d", lt - MEGASAVE_T1 + 1);
		msgout(lin);
	}

	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {
		eop = find_pilot(i, lt);

		if (eop > 0) {
			/* Valid pilot found, mark start of file */
			sof = i;
			i = eop;

			/* Check sync train ($64 up to to $FF) */
			for (h = 0; h < SYNCSEQSIZE; h++) {
				if (readttbyte(i + (h * BITSINABYTE), lp, sp, tp, en) != sv + h)
					break;
			}

			/* Sync train doesn't match */
			if (h != SYNCSEQSIZE)
				continue;

			/* Plausibility check: a non-zero value is expected after the sync sequence */
			if (readttbyte(i + (h * BITSINABYTE), lp, sp, tp, en) == 0x00)
				continue;

			/* Valid post-sync value found, mark start of data */
			sod = i + SYNCSEQSIZE * BITSINABYTE + BITSINABYTE;

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

			/* Point to the first pulse of the checkbyte (that's final) */
			eod = sod + (HEADERSIZE + x) * BITSINABYTE;

			/* Point to the last pulse of the checkbyte */
			eof = eod + BITSINABYTE - 1;

			/* Trace back from sof through any PREPILOTVALUE bytes (pre-leader) */
			xinfo = 0;
			while (readttbyte(sof - BITSINABYTE, lp, sp, tp, en) == PREPILOTVALUE) {
				sof -= BITSINABYTE;
				xinfo++;
			}

			if (addblockdef(lt, sof, sod, eod, eof, xinfo) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

void megasave_search(int lt)
{
	if (lt > 0) {
		megasave_search_core(lt);
	} else {
		int type, types[] = { MEGASAVE_T1, MEGASAVE_T2, MEGASAVE_T3, MEGASAVE_T4 };

		for (type = 0; type < sizeof(types)/sizeof(types[0]); type++) {
			megasave_search_core(types[type]);
		}
	}
}

int megasave_describe(int row)
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
	/* Note: No trailer has been documented, but we are not pretending it
	         here, just checking for it is future-proof */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync sequence */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= SYNCSEQSIZE;

	/* Exclude the post-sync byte from the count too */
	blk[row]->pilot_len -= 1;

	/* Exclude pre-pilot bytes from the count too */
	blk[row]->pilot_len -= blk[row]->xi;

	/* Extract and log extra useful info from this block */
	sprintf(lin, "\n - Pre-pilot byte count : %d", blk[row]->xi);
	strcat(info, lin);

	b = readttbyte(blk[row]->p2 - BITSINABYTE, lp, sp, tp, en);
	sprintf(lin, "\n - Post-sync value : $%02X", b);
	strcat(info, lin);

	sprintf(lin, "\n - Re-execute loader : %s", hd[EXEFLAG1OFFSET] ? "Yes" : "No");
	strcat(info, lin);
	if (!hd[EXEFLAG1OFFSET]) {
		if (hd[EXEFLAG2OFFSET]) {
			unsigned int exec = hd[EXECOFFSETL] + (hd[EXECOFFSETH] << 8);

			sprintf(lin, "\n - Execution by : JMP $%04X (SYS %d)", exec, exec);
			strcat(info, lin);
		} else {
			sprintf(lin, "\n - Execution by : BASIC RUN");
			strcat(info, lin);
		}
	}

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
