/*
 * turbotape_526.c (written by Luigi Di Fraia, Aug 2018)
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
 * Pilot: Byte (0x02, 1270 of them for Header block and 502 for Data block)
 * Sync: Sequence (bytes: 0x09 down to 0x01)
 * Header: Yes
 * Data: Continuous
 * Checksum: Yes
 * Post-data: No
 * Trailer: Only for Data block
 * Trailer homogeneous: Yes (bit 0 pulses, 2040 of them)
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	TT526_HEAD

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	9	/* amount of sync bytes */
#define MAXTRAILER	2040	/* max amount of trailer pulses read in (after Data only) */

#define HEADERSIZE	1	/* size of block header (for both Header and Data files) */

#define HDRPAYLOADSIZE	0xC0	/* size of the Header file payload */
#define NAMESIZE	0x20	/* size of the filename portion inside payload */

#define LOADOFFSETH	1	/* load location (MSB) offset inside payload */
#define LOADOFFSETL	0	/* load location (LSB) offset inside payload */
#define ENDOFFSETH	3	/* end location (MSB) offset inside payload */
#define ENDOFFSETL	2	/* end location (LSB) offset inside payload */
#define NAMEOFFSET	5	/* filename offset inside payload */

enum {
	FILE_TYPE_DATA = 0x00,
	FILE_TYPE_HEADER_RELOC = 0x01,
	FILE_TYPE_HEADER_NON_RELOC = 0x02
};

enum {
	STATE_SEARCH_HEADER = 0,
	STATE_SEARCH_DATA
};

/*
 * If defined Header file payload contents are extracted 
 * and consequently the CRC32 is calculated
 */
#define _TT526_EXTRACT_HEADER

void turbotape526_search(void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hdrpayload[HDRPAYLOADSIZE];	/* buffer to store Header file payload */

	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	/* Expected sync pattern */
	static int sypat[SYNCSEQSIZE] = {
		0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01
	};

	int ftype;			/* type of file */

	int xinfo;			/* extra info used in addblockdef() */

	int state;			/* whether to search for Header or Data */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	if (!quiet)
		msgout("  Turbotape 526");

	state = STATE_SEARCH_HEADER;	/* Initially search for a Header */

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

			/* Read file type from block header */
			ftype = readttbyte(sod, lp, sp, tp, en);

			switch (ftype) {
				/*
				 * Header files are treated the same way by the original
				 * code regardless of being relocatable or not
				 */
				case FILE_TYPE_HEADER_RELOC:
				case FILE_TYPE_HEADER_NON_RELOC:
					if (state != STATE_SEARCH_HEADER)
						continue;

					/* Read Header file payload */
					for (h = 0; h < HDRPAYLOADSIZE; h++) {
						hdrpayload[h] = readttbyte(sod + (HEADERSIZE + h) * BITSINABYTE, lp, sp, tp, en);
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

					/* Point to the first pulse of the post-payload byte (that's final) */
					eod = sod + (HEADERSIZE + HDRPAYLOADSIZE - 1 + 1 /* Include an extra post-payload byte, 0x02 */) * BITSINABYTE;

					/* Point to the last pulse of the post-payload byte */
					eof = eod + BITSINABYTE - 1;

					if (addblockdefex(TT526_HEAD, sof, sod, eod, eof, xinfo, ftype) >= 0) {
						state = STATE_SEARCH_DATA;
						i = eof;	/* Search for further files starting from the end of this one */
					}

					break;

				case FILE_TYPE_DATA:	/* Data */
					if (state != STATE_SEARCH_DATA)
						continue;

					/* 
					 * Check if the beginning of this Data file (sof) is
					 * close enough to its corresponding Header file's end (eof)
					 * Note: 2000 pulses = half Data file pilot length, in case
					 *       half the pilot is damaged
					 */
					if (sof - eof > 2000) {
						state = STATE_SEARCH_HEADER;
						continue;
					}

					/* Point to the first pulse of the checkbyte (that's final) */
					eod = sod + (HEADERSIZE + x) * BITSINABYTE;

					/* Initially point to the last pulse of the checkbyte */
					eof = eod + BITSINABYTE - 1;

					/* Trace 'eof' to end of trailer (bit 0 pulses only) */
					h = 0;
					while (eof < tap.len - 1 &&
							h++ < MAXTRAILER &&
							readttbit(eof + 1, lp, sp, tp) == 0)
						eof++;

					if (addblockdefex(TT526_DATA, sof, sod, eod, eof, xinfo, ftype) >= 0)
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

int turbotape526_describe(int row)
{
	int i, s;
	int ftype, lt;
	int en, tp, sp, lp;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Set read pointer to the beginning of the data */
	s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);

	/* Compute pilot & trailer lengths */

	/* pilot is in bytes... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / BITSINABYTE;

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync sequence */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= SYNCSEQSIZE;

	/* Read file type */
	ftype = blk[row]->meta1;
	sprintf(lin, "\n - FILE type : $%02X", ftype);
	strcat(info, lin);

	lt = blk[row]->lt;

	if (lt == TT526_HEAD) {
		int dfs, dfe;
		int hdrpayload[HDRPAYLOADSIZE];
		char bfname[NAMESIZE + 1], bfnameASCII[NAMESIZE + 1];

		/* Read Header payload (it's safe to read it here for it was already decoded during the search stage) */
		for (i = 0; i < HDRPAYLOADSIZE; i++)
			hdrpayload[i] = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, en);

		/* Read filename */
		for (i = 0; i < NAMESIZE; i++)
			bfname[i] = hdrpayload[NAMEOFFSET + i];
		bfname[i] = '\0';

		trim_string(bfname);
		pet2text(bfnameASCII, bfname);

		if (blk[row]->fn != NULL)
			free(blk[row]->fn);
		blk[row]->fn = (char*)malloc(strlen(bfnameASCII) + 1);
		strcpy(blk[row]->fn, bfnameASCII);

		/* Set load and end locations and size */
		blk[row]->cs = 0x0100;
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

#ifdef _TT526_EXTRACT_HEADER
		/* Copy Header payload contents */

		if (blk[row]->dd != NULL)
			free(blk[row]->dd);

		blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

		for (i = 0; i < blk[row]->cx; i++)
			blk[row]->dd[i] = (unsigned char)hdrpayload[i];
#endif

		blk[row]->rd_err = rd_err;
	} else {
		int cb;

		/* Retrieve C64 memory location for data load/end address from extra-info */
		blk[row]->cs = blk[row]->xi & 0xFFFF;
		blk[row]->ce = (blk[row]->xi & 0xFFFF0000) >> 16;
		blk[row]->cx = (blk[row]->ce - blk[row]->cs) + 1;

		/* Extract Data and test checksum... */
		rd_err = 0;
		cb = 0;

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
	}

	return(rd_err);
}
