/*
 * gremlin_gbh.c (written by Luigi Di Fraia, Apr 2020)
 * Based on turbotape_526.c and graphicadventurecreator.c
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
 * Pilot: Pilot pulse (at least 1200 of them)
 * Sync: Bit 0 pulse
 * Header: Yes
 * Data: Continuous
 * Checksum: No
 * Post-data: No
 * Trailer: None
 * Trailer homogeneous: N/A
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	GREMLIN_GBH_HEAD

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	1	/* amount of sync pulses */
#define MAXTRAILER	40	/* max amount of trailer pulses read in */

#define HDRPAYLOADSIZE	0x30	/* size of the Header file payload */

#define LOADOFFSETH	1	/* load location (MSB) offset inside payload */
#define LOADOFFSETL	0	/* load location (LSB) offset inside payload */
#define ENDOFFSETH	3	/* end location (MSB) offset inside payload */
#define ENDOFFSETL	2	/* end location (LSB) offset inside payload */

enum {
	STATE_SEARCH_HEADER = 0,
	STATE_SEARCH_DATA
};

/*
 * If defined Header file payload contents are extracted 
 * and consequently the CRC32 is calculated
 */
#define _GREMLIN_GBH_EXTRACT_HEADER

void gremlin_gbh_search(void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hdrpayload[HDRPAYLOADSIZE];	/* buffer to store Header file payload */

	int en, tp, sp, mp, lp, sv;	/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int xinfo;			/* extra info used in addblockdef() */

	int state;			/* whether to search for Header or Data */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	mp = ft[THISLOADER].mp;
	lp = ft[THISLOADER].lp;
	sv = ft[THISLOADER].sv;

	if (!quiet)
		msgout("  Gremlin GBH");

	state = STATE_SEARCH_HEADER;	/* Initially search for a Header */

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

			/* Valid sync bit found, mark start of data */
			sod = i;

			switch (state) {
				case STATE_SEARCH_HEADER:
					/* Read Header file payload */
					for (h = 0; h < HDRPAYLOADSIZE; h++) {
						hdrpayload[h] = readttbyte(sod + h * BITSINABYTE, mp, sp, tp, en);
						if (hdrpayload[h] == -1)
							break;
					}
					/* In case of read error we cannot trust the contents so give up on this Header file */
					if (h != HDRPAYLOADSIZE)
						continue;

					/* Extract load and end locations */
					s = hdrpayload[LOADOFFSETL] + (hdrpayload[LOADOFFSETH] << 8);
					e = hdrpayload[ENDOFFSETL]  + (hdrpayload[ENDOFFSETH]  << 8);

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

					/* Store the info read from payload as extra-info */
					xinfo = s + (e << 16);

					/* Point to the first pulse of the last payload byte (that's final) */
					eod = sod + (HDRPAYLOADSIZE - 1) * BITSINABYTE;

					/* Point to the last pulse of the last payload byte */
					eof = eod + BITSINABYTE - 1;

					/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
					h = 0;
					while (eof < tap.len - 1 &&
							h++ < MAXTRAILER &&
							readttbit(eof + 1, mp, sp, tp) >= 0)
						eof++;

					if (addblockdef(GREMLIN_GBH_HEAD, sof, sod, eod, eof, xinfo) >= 0) {
						state = STATE_SEARCH_DATA;
						i = eof;	/* Search for further files starting from the end of this one */
					}

					break;

				case STATE_SEARCH_DATA:
					/* 
					 * Check if the beginning of this Data file (sof) is
					 * close enough to its corresponding Header file's end (eof)
					 * Note: 1200 pulses = half Data file pilot length, in case
					 *       half the pilot is damaged
					 */
					if (sof - eof > 1200) {
						state = STATE_SEARCH_HEADER;
						continue;
					}

					/* TODO: create overrides based on the Header's CRC32 instead of s/e values */

					/* Impossamole: 2nd turbo file - load address override */
					if (s == 0x2000 && e == 0xF7FF) { 
						s = 0x7EC0;
						x = 0x7940;
						xinfo = s + (e << 16);
					}

					/* Point to the first pulse of the last data byte (that's final) */
					eod = sod + (x - 1) * BITSINABYTE;

					/* Initially point to the last pulse of the last data byte */
					eof = eod + BITSINABYTE - 1;

					/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
					h = 0;
					while (eof < tap.len - 1 &&
							h++ < MAXTRAILER &&
							readttbit(eof + 1, mp, sp, tp) >= 0)
						eof++;

					if (addblockdef(GREMLIN_GBH_DATA, sof, sod, eod, eof, xinfo) >= 0)
						i = eof;	/* Search for further files starting from the end of this one */

					/* Back to searching for Header even if we failed adding block here */
					state = STATE_SEARCH_HEADER;

					break;

				default:
					continue;
			}
		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int gremlin_gbh_describe(int row)
{
	int i, s;
	int lt;
	int en, tp, sp, mp;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	mp = ft[THISLOADER].mp;

	/* Set read pointer to the beginning of the data */
	s = blk[row]->p2;

	/* Compute pilot & trailer lengths */

	/* pilot is in pulses... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1);

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync sequence */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= SYNCSEQSIZE;

	lt = blk[row]->lt;

	if (lt == GREMLIN_GBH_HEAD) {
		int dfs, dfe;
		int hdrpayload[HDRPAYLOADSIZE];
		//char bfname[NAMESIZE + 1], bfnameASCII[NAMESIZE + 1];

		/* Read Header payload (it's safe to read it here for it was already decoded during the search stage) */
		for (i = 0; i < HDRPAYLOADSIZE; i++)
			hdrpayload[i] = readttbyte(s + (i * BITSINABYTE), mp, sp, tp, en);

		/* Set load and end locations and size */
		blk[row]->cs = 0x0F25F;
		blk[row]->ce = blk[row]->cs + HDRPAYLOADSIZE - 1;
		blk[row]->cx = HDRPAYLOADSIZE;

		/* Retrieve C64 memory location for data load/end address from extra-info */
		dfs = blk[row]->xi & 0xFFFF;
		dfe = (blk[row]->xi & 0xFFFF0000) >> 16;

		sprintf(lin,"\n - DATA FILE Load address : $%04X", dfs);
		strcat(info,lin);
		sprintf(lin,"\n - DATA FILE End address : $%04X", dfe);
		strcat(info,lin);

		rd_err = 0;

#ifdef _GREMLIN_GBH_EXTRACT_HEADER
		/* Copy Header payload contents */

		if (blk[row]->dd != NULL)
			free(blk[row]->dd);

		blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

		for (i = 0; i < blk[row]->cx; i++)
			blk[row]->dd[i] = (unsigned char)hdrpayload[i];
#endif

		blk[row]->rd_err = rd_err;
	} else {
		/* Retrieve C64 memory location for data load/end address from extra-info */
		blk[row]->cs = blk[row]->xi & 0xFFFF;
		blk[row]->ce = (blk[row]->xi & 0xFFFF0000) >> 16;
		blk[row]->cx = (blk[row]->ce - blk[row]->cs) + 1;

		/* Extract Data and test checksum... */
		rd_err = 0;

		if (blk[row]->dd != NULL)
			free(blk[row]->dd);

		blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

		for (i = 0; i < blk[row]->cx; i++) {
			b = readttbyte(s + (i * BITSINABYTE), mp, sp, tp, en);

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
	}

	return(rd_err);
}
