/*
 * glass.c (by Luigi Di Fraia, Mar 2018)
 *
 * Part of project "TAPClean". May be used in conjunction with "Final TAP".
 *
 * Based on turrican.c, which is part of "TAPClean".
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

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	GLASS_HEAD

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define HEADERSIZE	1	/* size of block header (for both Header and Data files) */

#define HDRPAYLOADSIZE	17	/* size of the Header file payload */
#define NAMESIZE	10	/* size of the filename portion inside payload */

#define STOPFLAGOFFSET	0	/* flag offset inside payload */
#define LOADOFFSETH	14	/* load location (MSB) offset inside payload */
#define LOADOFFSETL	13	/* load location (LSB) offset inside payload */
#define DATAOFFSETH	12	/* data size (MSB) offset inside payload */
#define DATAOFFSETL	11	/* data size (LSB) offset inside payload */
#define NAMEOFFSET	1	/* filename offset inside payload */

enum {
	FILE_TYPE_HEADER = 0x00,
	FILE_TYPE_DATA = 0xFF
};

enum {
	STOP_FLAG_LAST = 0x03,
	STOP_FLAG_MORE_TO_COME = 0x07,
	STOP_FLAG_SPECTRUM = 0x00
};

enum {
	STATE_SEARCH_HEADER = 0,
	STATE_SEARCH_DATA
};

static int glass_find_pilot (int pos, int lt)
{
	int z, sp, lp, tp, pmin;

	sp = ft[lt].sp;
	lp = 0xA0;
	pmin = ft[lt].pmin;

	tp = 0x76;

	if (readttbit(pos, lp, sp, tp) == 1) {
		z = 0;

		while (readttbit(pos, lp, sp, tp) == 1 && pos < tap.len) {
			z++;
			pos++;
		}

		if (z == 0)
			return 0;

		if (z < pmin)
			return -pos;

		if (z >= pmin)
			return pos;
	}

	return 0;
}

void glass_search (void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hdrpayload[HDRPAYLOADSIZE];	/* buffer to store Header file payload */

	int en, tp, sp, lp, sv;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int ftype;			/* type of file */

	int xinfo;			/* extra info used in addblockdef() */

	int state;			/* whether to search for header or data */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;
	sv = ft[THISLOADER].sv;

	if (!quiet)
		msgout("  Glass Tape");

	state = STATE_SEARCH_HEADER;	/* Initially search for a header */

	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {

		eop = glass_find_pilot(i, state == STATE_SEARCH_HEADER ? GLASS_HEAD : GLASS_DATA);

		if (eop > 0) {
			/* Valid pilot found, mark start of header file */
			sof = i;
			i = eop;

			/* Check if there's a valid sync bit for this loader */
			if (readttbit(i, lp, sp, tp) != sv)
				continue;

			/* Take into account sync bit */
			i++;

			/* Valid sync bit found, mark start of data */
			sod = i;

			/* Read file type from block header */
			ftype = readttbyte(sod, lp, sp, tp, en);

			switch (ftype) {

				case FILE_TYPE_HEADER:
					if (state != STATE_SEARCH_HEADER)
						continue;

					/* Read header file payload */
					for (h = 0; h < HDRPAYLOADSIZE; h++) {
						hdrpayload[h] = readttbyte(sod + (HEADERSIZE + h) * BITSINABYTE, lp, sp, tp, en);
						if (hdrpayload[h] == -1)
							break;
					}

					/* In case of read error we cannot trust the contents so give up on this Header file */
					if (h != HDRPAYLOADSIZE)
						continue;
//printf("\nStop flag: $%02X", hdrpayload[STOPFLAGOFFSET]);
					/* Plausibility check */
					if (hdrpayload[STOPFLAGOFFSET] != STOP_FLAG_LAST && 
							hdrpayload[STOPFLAGOFFSET] != STOP_FLAG_MORE_TO_COME &&
							hdrpayload[STOPFLAGOFFSET] != STOP_FLAG_SPECTRUM)
						continue;

					/* Extract load location and size */
					s = hdrpayload[LOADOFFSETL] + (hdrpayload[LOADOFFSETH] << 8);
					x = hdrpayload[DATAOFFSETL] + (hdrpayload[DATAOFFSETH] << 8);

					/* Compute C64 memory location of the _LAST loaded byte_ */
					e = s + x - 1;
//printf("\nStart: %x, End: %x", s, e);
					/* Plausibility check */
					if (e > 0xFFFF)
						continue;

					/* Store the info read from payload as extra-info */
					xinfo = s + (e << 16);

					/* Point to the first pulse of the checkbyte (that's final) */
					eod = sod + (HEADERSIZE + HDRPAYLOADSIZE - 1 + 1 /* Include checkbyte */) * BITSINABYTE;

					/* Point to the last pulse of the checkbyte */
					eof = eod + BITSINABYTE - 1;
//printf("\nHeader! sof = %x, eof = %x\n", sof, eof);

					if (addblockdefex(GLASS_HEAD, sof, sod, eod, eof, xinfo, ftype) >= 0) {
						state = STATE_SEARCH_DATA;
						i = eof;	/* Search for further files starting from the end of this one */
					}

					break;

				case FILE_TYPE_DATA:
					if (state != STATE_SEARCH_DATA)
						continue;

					/* 
					 * Check if the beginning of this Data file (sof) is
					 * close enough to its corresponding Header file's end (eof)
					 * Note: 0x300 pulses = half Data file pilot length, in case
					 *       half the pilot is damaged
					 */
					if (sof - eof > 0x300) {
						state = STATE_SEARCH_HEADER;
						continue;
					}

					/* Point to the first pulse of the checkbyte (that's final) */
					eod = sod + (HEADERSIZE + x) * BITSINABYTE;

					/* Initially point to the last pulse of the checkbyte */
					eof = eod + BITSINABYTE - 1;
//printf("\nData! sof = %x, eof = %x\n", sof, eof);

					if (addblockdefex(GLASS_DATA, sof, sod, eod, eof, xinfo, ftype) >= 0)
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

int glass_describe (int row)
{
	int i, s;
	int ftype, lt;
	int en, tp, sp, lp;

	int b, cb, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Set read pointer to the beginning of the payload */
	s = blk[row]->p2;

	/* Compute pilot & trailer lengths */

	/* pilot is in bytes... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / BITSINABYTE;

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* Read file type */
	ftype = blk[row]->meta1;
	sprintf(lin, "\n - FILE type : $%02X (%s)", ftype, ftype == FILE_TYPE_HEADER ? "Header" : "Data");
	strcat(info, lin);

	lt = blk[row]->lt;

	if (lt == GLASS_HEAD) {
		unsigned char stopflag;
		int dfs, dfe;
		int hd[HEADERSIZE + HDRPAYLOADSIZE];
		char bfname[NAMESIZE + 1], bfnameASCII[NAMESIZE + 1];

		/* Read Header payload (it's safe to read it here for it was already decoded during the search stage) */
		for (i = 0; i < HEADERSIZE + HDRPAYLOADSIZE; i++)
			hd[i]= readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);

		/* Read filename */
		for (i = 0; i < NAMESIZE; i++)
			bfname[i] = hd[HEADERSIZE + NAMEOFFSET + i];
		bfname[i] = '\0';

		trim_string(bfname);
		pet2text(bfnameASCII, bfname);

		if (blk[row]->fn != NULL)
			free(blk[row]->fn);
		blk[row]->fn = (char*)malloc(strlen(bfnameASCII) + 1);
		strcpy(blk[row]->fn, bfnameASCII);

		/* Read stop flag */
		stopflag = hd[HEADERSIZE + STOPFLAGOFFSET];

		/* Set load and end locations and size */
		blk[row]->cs = 0x090D;
		blk[row]->ce = blk[row]->cs + HEADERSIZE + HDRPAYLOADSIZE - 1;
		blk[row]->cx = HEADERSIZE + HDRPAYLOADSIZE;

		/* Retrieve C64 memory location for data load/end address from extra-info */
		dfs = blk[row]->xi & 0xFFFF;
		dfe = (blk[row]->xi & 0xFFFF0000) >> 16;

		sprintf(lin,"\n - DATA FILE Load address : $%04X", dfs);
		strcat(info,lin);
		sprintf(lin,"\n - DATA FILE End address : $%04X", dfe);
		strcat(info,lin);
		sprintf(lin,"\n - Last file in current chain : %s", stopflag == STOP_FLAG_LAST ? "Yes" : "No");
		strcat(info,lin);

		/* Extract Data and test checksum... */
		rd_err = 0;
		cb = 0;

		/* Copy Header contents */

		if (blk[row]->dd != NULL)
			free(blk[row]->dd);

		blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

		for (i = 0; i < blk[row]->cx; i++) {
			blk[row]->dd[i] = (unsigned char)hd[i];
			cb ^= (unsigned char)hd[i];
		}

		b = readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);
		if (b == -1) {
			/* Even if not within data, we cannot validate data reliably if
			   checksum is unreadable, so that increase read errors */
			rd_err++;

			/* for experts only */
			sprintf(lin, "\n - Read Error on checkbyte @$%X", s + ((HEADERSIZE + i) * BITSINABYTE));
			strcat(info, lin);
		}

		blk[row]->cs_exp = cb & 0xFF;
		blk[row]->cs_act = b  & 0xFF;
		blk[row]->rd_err = rd_err;

	} else {
		/* Retrieve C64 memory location for data load/end address from extra-info */
		blk[row]->cs = blk[row]->xi & 0xFFFF;
		blk[row]->ce = (blk[row]->xi & 0xFFFF0000) >> 16;
		blk[row]->cx = (blk[row]->ce - blk[row]->cs) + 1;

		/* Extract Data and test checksum... */
		rd_err = 0;
		cb = FILE_TYPE_DATA;	/* For data the check includes the file type value */

		if (blk[row]->dd != NULL)
			free(blk[row]->dd);

		blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

		for (i = 0; i < blk[row]->cx; i++) {
			b = readttbyte(s + ((HEADERSIZE + i) * BITSINABYTE), lp, sp, tp, en);
			cb ^= b;

			if (b != -1) {
				blk[row]->dd[i] = b;
			} else {
				blk[row]->dd[i] = 0x69;	/* sentinel error value */
				rd_err++;

				/* for experts only */
				sprintf(lin, "\n - Read Error on byte @$%X (prg data offset: $%04X)", s + ((HEADERSIZE + i) * BITSINABYTE), i + 2);
				strcat(info, lin);
			}
		}

		b = readttbyte(s + ((HEADERSIZE + i) * BITSINABYTE), lp, sp, tp, en);
		if (b == -1) {
			/* Even if not within data, we cannot validate data reliably if
			   checksum is unreadable, so that increase read errors */
			rd_err++;

			/* for experts only */
			sprintf(lin, "\n - Read Error on checkbyte @$%X", s + ((HEADERSIZE + i) * BITSINABYTE));
			strcat(info, lin);
		}

		blk[row]->cs_exp = cb & 0xFF;
		blk[row]->cs_act = b  & 0xFF;
		blk[row]->rd_err = rd_err;
	}

	return(rd_err);
}
