/*
 * lexpeed.c (by Luigi Di Fraia, Oct 2018)
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
 * Pilot: Byte (0x02, 1272 of them)
 * Sync: Sequence (bytes: 0x09 down to 0x01)
 * Header: Yes
 * Data: Continuous
 * Checksum: Yes
 * Post-data: No
 * Trailer: Yes
 * Trailer homogeneous: Yes (bit 0 pulses, 2040 of them)
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	LEXPEED

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	10	/* amount of sync bytes */
#define MAXTRAILER	2040	/* max amount of trailer pulses read in */

#define CODESIZE	12	/* size of code checked in master loader block */

#define LOADOFFSETH	1	/* load location (MSB) offset inside file record */
#define LOADOFFSETL	0	/* load location (LSB) offset inside file record */
#define ENDOFFSETH	3	/* end  location (MSB) offset inside file record */
#define ENDOFFSETL	2	/* end  location (LSB) offset inside file record */
#define EXECOFFSETH	5	/* execution address (MSB) offset inside file record */
#define EXECOFFSETL	4	/* execution address (LSB) offset inside file record */

#define MASTERLOADSIZE	0x100	/* size in bytes of the first turbo file */

#define LEXPEED_CBM_DATA_SIZE	0xB3	/* size in bytes of the CBM Data file */

#ifdef _MSC_VER
#define inline __inline
#endif

//#define LEXPEED_DEBUG

static inline void get_lexpeed_addresses (int *buf, int bufsz, int blkindex, unsigned int *s, unsigned int *e, unsigned int *exec)
{
	int base;

	blkindex -= 2;		/* Convert to a 0-based offset */

	base = 0xb7 + blkindex * 6;
#ifdef LEXPEED_DEBUG
	printf ("\nBase offset for record lookup: $%04X", 0x0900 + base);
#endif

	if (blkindex < 0 || base + 6 > bufsz) {
		*s = 0;
		*e = 0;
		*exec = 0;
#ifdef LEXPEED_DEBUG
		printf ("\nInvalid block offset");
#endif
	} else {
		*s  = buf[base + LOADOFFSETL];
		*s |= buf[base + LOADOFFSETH] << 8;
		*e  = buf[base + ENDOFFSETL];
		*e |= buf[base + ENDOFFSETH] << 8;
		*exec  = buf[base + EXECOFFSETL];
		*exec |= buf[base + EXECOFFSETH] << 8;
#ifdef LEXPEED_DEBUG
		printf ("\ns: $%04X, e: $%04X, exec: $%04X", *s, *e, *exec);
#endif
	}
}

static void lexpeed_search_core(int slice_start, int slice_end)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */

	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int s, e, exec;	/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	/* Expected sync pattern */
	static int sypat[SYNCSEQSIZE] = {
		0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00
	};
	/* Expected code pattern */
	static int codepat[CODESIZE] = {
		0xA9, 0x00, 0x85, 0xB4, 0xA9, XX, 0x85, 0xBE, 0xA9, XX, 0x85, 0xBF
	};

	int lexpeed_index;		/* Index of file (0 = master loader) */

	int xinfo, meta1;		/* extra info used in addblockdef() */

	int masterloader[MASTERLOADSIZE];	/* Buffer for master loader (first turbo file, 0x100 bytes) */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	lexpeed_index = 0;

	for (i = slice_start; i > 0 && i < slice_end - BITSINABYTE; i++) {
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

			/* Read first segment from file */
			if (lexpeed_index == 0) {
				int b;

				for (h = 0; h < CODESIZE; h++) {
					if (codepat[h] == XX)
						continue; 
					if (readttbyte(sod + h * BITSINABYTE, lp, sp, tp, en) != codepat[h])
						break;
				}

				/* Bail out if there was an error reading the block header */
				if (h != CODESIZE)
					continue;

				/* Store master loader */
				for (h = 0; h < MASTERLOADSIZE; h++) {
					b = readttbyte(sod + (h  * BITSINABYTE), lp, sp, tp, en);
					if (b == -1)
						break; /* Don't break here: during debug we will see how many errors occur */
					else
						masterloader[h] = b;
				}

				if (h == MASTERLOADSIZE) {
					lexpeed_index++;
					s = masterloader[9] << 8;
					e = s + 0x0100;
					exec = s;
#ifdef LEXPEED_DEBUG
					printf ("\nLexpeed master loader decoded");
#endif
				} else {
					/* We can't decode in case of error */
					lexpeed_index = -1;
					s = e = exec = 0;
				}
			} else if (lexpeed_index > 0) {
				lexpeed_index++;
				get_lexpeed_addresses (masterloader, MASTERLOADSIZE, lexpeed_index, &s, &e, &exec);
			}

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
			eod = sod + x * BITSINABYTE;

			/* Initially point to the last pulse of the checkbyte */
			eof = eod + BITSINABYTE - 1;

			/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) >= 0)
				eof++;

			/* Store the info read from master loader as extra-info */
			xinfo = s + (e << 16);
			meta1 = exec;

			if (addblockdefex(THISLOADER, sof, sod, eod, eof, xinfo, meta1) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

void lexpeed_search(void)
{
	int cbm_index = 1;

	if (!quiet)
		msgout("  Lexpeed Fastsave System");

	for (;;) {
		int match;
		int ib, slice_start, slice_end;

		match = 0;

		//find_decode_block(CBM_HEAD, cbm_index);	/* Required if we were to extract the load address */
		ib  = find_decode_block(CBM_DATA, cbm_index);
		if (ib == -1) {

			break;	/* No further titles on this tape image */

		} else if ((unsigned int) blk[ib]->cx == LEXPEED_CBM_DATA_SIZE) {

			int buf[LEXPEED_CBM_DATA_SIZE];

			int seq_vectors[10] = {
				0x34, 0x03, 0xED, 0xF6, 0x3E, 0xF1, 0x2F, 0xF3, 0x66, 0xFE
			};

			int index, offset;

			/* Make an 'int' copy for use in find_seq() */
			for (index = 0; index < LEXPEED_CBM_DATA_SIZE; index++)
				buf[index] = blk[ib]->dd[index];

			/* We now look for invariants to confirm this is actually a Lexpeed instance */

			match = 1;

			offset = find_seq(buf, LEXPEED_CBM_DATA_SIZE, seq_vectors, sizeof(seq_vectors) / sizeof(seq_vectors[0]));
			if (offset != 0)
				match = 0;

			slice_start = blk[ib]->p4 + 1;	/* Set to CBM Dta end + 1 */
		}

		/* Parameter extraction successful? */
		if (match == 0) {
			cbm_index += 2;
			continue;
		}

		/* 
		 * Search for the next set of CBM files, if any, because we only 
		 * want to look for a Lexpeed file chain in between two CBM boots
		 */
		do {

			ib = find_decode_block(CBM_HEAD, cbm_index + 2);
			if (ib == -1)
				slice_end = tap.len;
			else
				slice_end = blk[ib]->p1;

			cbm_index += 2;

		} while (slice_end < slice_start);

		/*
		 * Optimize search: If there's a CBM Data (repeated) file 
		 * in between, then start scanning from its end
		 */
		ib  = find_decode_block(CBM_DATA, cbm_index - 1);
		if (ib != -1) {
			if (blk[ib]->p4 < slice_end)
				slice_start = blk[ib]->p4 + 1;
		}

		sprintf(lin,
			" - scanning from $%04X to $%04X", 
			slice_start,
			slice_end);
		msgout(lin);

		lexpeed_search_core(slice_start, slice_end);
	}
}

int lexpeed_describe(int row)
{
	int i, s;
	unsigned int exec;
	int en, tp, sp, lp;
	int cb;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Set read pointer to the beginning of the payload */
	s = blk[row]->p2;

	/* Retrieve C64 memory location for load/end address from extra-info */
	blk[row]->cs = blk[row]->xi & 0xFFFF;
	blk[row]->ce = (blk[row]->xi & 0xFFFF0000) >> 16;
	blk[row]->cx = (blk[row]->ce - blk[row]->cs) + 1;

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

	/* Extract execution address and print it out */
	exec = blk[row]->meta1;
	sprintf(lin, "\n - Exe Address : $%04X (%s)", exec, exec ? "in use" : "unused");
	strcat(info, lin);

	/* Extract data and test checksum... */
	rd_err = 0;
	cb = 0;

	s = blk[row]->p2;

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
