/*
 * fftape.c (by Luigi Di Fraia, Oct 2009)
 * Based on biturbo.c
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
 * CBM inspection needed: No (load/end addresses are hardcoded but these are the same for any program)
 * Single on tape: No (ref. 'The Thriller Pack')
 * Sync: Bit + Byte
 * Header: No
 * Data: Continuous (Note: file #3 is made of non-contiguous-memory blocks)
 * Checksum: No
 * Post-data: No
 * Trailer: Yes
 * Trailer homogeneous: Yes (bit 0 pulses)
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	FFTAPE

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SPARESYNCVAL	0xAA	/* value of the spare sync byte */
#define SPRSYNCSEQSIZE	1	/* amount of spare sync bytes */
#define MAXTRAILER	2040	/* max amount of trailer pulses read in */

#define FILESINACHAIN	4	/* 4 files for each CBM boot file */

#define LOADADDRFILE1	0x6000	/* load address of first file */
#define ENDADDRFILE1	0x7400	/* end address+1 of first file */
#define LOADADDRFILE2	0x1400	/* load address of second file */
#define ENDADDRFILE2	0x23E0	/* end address+1 of second file */
#define LOADADDRFILE3	0x2400	/* load address of third file */
#define ENDADDRFILE3	0xFFFE	/* end address of third file (maximum) */
#define LOADADDRFILE4	0x1400	/* load address of fourth file */
#define ENDADDRFILE4	0x2400	/* end address+1 of fourth file */

#define PACKTABLEOFFSET	0x0C00	/* unpack table is at $2000, file loads at $1400 */
#define PACKTABLESIZE	0x200	/* size in bytes */
#define PACKTABLEENTR	(PACKTABLESIZE / 2 - 1)	/* max entries */

/* If defined, the contents of the unpack table are displayed in the report */
//#define DEBUGFFTABLESRAW

/* If defined, the contents of the unpack table are decoded and displayed in
 * the report (only if DEBUGFFTABLESRAW is not defined)
 */
//#define DEBUGFFTABLES

static unsigned char unpackt[PACKTABLESIZE];	/* Table to unpack blocks */

#ifdef _MSC_VER
#define inline __inline
#endif

static inline unsigned int get_packed_address (int offset)
{
	return ((unsigned int) unpackt[offset] + (((unsigned int) unpackt[offset+0x0100]) << 8));
}

static inline unsigned int get_packed_file_end_address (void)
{
	int i;
	unsigned int s, e, x;

	s = LOADADDRFILE3;	/* init load address */
	x = 0;			/* init size */

	for (i = 0; i < PACKTABLEENTR; i += 2) {
		e = get_packed_address(i);
		if (e < LOADADDRFILE3)
			e = ENDADDRFILE3;
		if (e > ENDADDRFILE3)
			e = ENDADDRFILE3;
		if (e > s) {
			x += (e - s);
			if (e == ENDADDRFILE3)
				break;
			s = get_packed_address(i + 1);
		} else {
			break;
		}
	}

	return (LOADADDRFILE3 + x);
}

void fftape_search (void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */


	int en, tp, sp, lp, sv;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int ff_index;			/* Index of FF file in current chain of 4 files */

	unsigned int ff3_e;		/* block #3 end location referred to C64 memory */

	int xinfo;			/* extra info used in addblockdef() */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;
	sv = ft[THISLOADER].sv;

	if (!quiet)
		msgout("  Freeze Frame tape");

	ff_index = 1;
	ff3_e = 0x0001;

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

			/* Check if there's a valid spare sync byte for this loader */
			if (readttbyte(i, lp, sp, tp, en) != SPARESYNCVAL)
				continue;

			/* Valid sync bit + sync byte found, mark start of data */
			sod = i + BITSINABYTE * SPRSYNCSEQSIZE;

			switch (ff_index) {
				/* Values for first and second block in a chain are
				 * hardcoded and the same for each program, it's not
				 * worth extracting them from CBM data block
				 */
				case 1:
					s = LOADADDRFILE1;
					e = ENDADDRFILE1;
					break;
				case 2:
					s = LOADADDRFILE2;
					e = ENDADDRFILE2;
					break;
				case 3:
					s = LOADADDRFILE3;
					e = ff3_e;
					break;
				/* Values for fourth block in a chain are hardcoded too */
				case 4:
					s = LOADADDRFILE4;
					e = ENDADDRFILE4;
					break;
				/* We should not get here at all */
				default:
					/* This will make the plausibility check fail */
					s = 0x0001;
					e = 0x0001;
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

			/* Point to the first pulse of the last data byte (that's final) */
			eod = sod + (x - 1) * BITSINABYTE;

			/* Initially point to the last pulse of the last byte */
			eof = eod + BITSINABYTE - 1;

			/* Trace 'eof' to end of trailer (bit 0 pulses only) */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) == 0)
				eof++;

			/* Store the load/end addresses as extra-info */
			xinfo = s + (e << 16);

			if (addblockdef(THISLOADER, sof, sod, eod, eof, xinfo) >= 0) {
				i = eof;	/* Search for further files starting from the end of this one */

				/* Calculate end address for third file (readttbyte is quite safe here) */
				if (ff_index == 2) {
					int j, b, rd_err;

					rd_err = 0;

					/* Store unpack table */
					for (j = 0; j < PACKTABLESIZE; j++) {
						b = readttbyte(sod + ((j + PACKTABLEOFFSET) * BITSINABYTE), lp, sp, tp, en);
						if (b == -1)
							rd_err++; /* Don't break here: during debug we will see how many errors occur */
						else
							unpackt[j] = b;
					}

					/* Calculate end address of packed file only if unpack table is not damaged */
					if (rd_err == 0)
						ff3_e = get_packed_file_end_address();
					else
						ff3_e = 0x0001;
				}

				/* File can be acknowledged now */
				ff_index++;

				/* Prepare for next chain */
				if (ff_index > FILESINACHAIN) {
					ff_index = 1;
					ff3_e = 0x0001;
				}
			} else if (ff_index == 3) {
				/* If file 3 is unrecognized, then try look for file 4 */
				ff_index++;
			}

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int fftape_describe(int row)
{
	int i, s;
	int en, tp, sp, lp;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Retrieve C64 memory location for load/end address from extra-info */
	blk[row]->cs = blk[row]->xi & 0xFFFF;
	blk[row]->ce = (blk[row]->xi & 0xFFFF0000) >> 16;
	blk[row]->cx = (blk[row]->ce - blk[row]->cs) + 1;

	/* Compute pilot & trailer lengths */

	/* don't forget sync is 9 bits (1 bit + 1 byte)... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1 - 1 - BITSINABYTE);
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* Extract data */
	rd_err = 0;

	s = blk[row]->p2;

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

#if defined(DEBUGFFTABLES) || defined(DEBUGFFTABLESRAW)
	/* Unpack table - stored inside the second file of each chain */
	if (blk[row]->cs == LOADADDRFILE2 && blk[row]->ce + 1 == ENDADDRFILE2) {
#ifdef DEBUGFFTABLESRAW
		strcat(info, "\n - Unpack table (LSBs) :");
		for (i = 0; i < PACKTABLESIZE; i++) {
			if (i == PACKTABLESIZE/2)
				strcat(info, "\n - Unpack table (MSBs) :");

			if (i % 0x10 == 0)
				strcat(info, "\n   ");

			b = readttbyte(s + ((i + PACKTABLEOFFSET) * BITSINABYTE), lp, sp, tp, en);

			// Do NOT increase read errors for this one is not within DATA
			if (b == -1) {
				strcat(info, "-- ");
			} else {
				sprintf(lin, "%02X ", b);
				strcat(info, lin);
			}
		}
#else
		unsigned int ps, pe;

		ps = LOADADDRFILE3;

		/* Copy the current table into buffer */
		for (i = 0; i < PACKTABLESIZE; i++) {
			b = readttbyte(s + ((i + PACKTABLEOFFSET) * BITSINABYTE), lp, sp, tp, en);
			if (b != -1) {
				unpackt[i] = b;
			} else {
				break;
			}
		}

		if (i < PACKTABLESIZE) {
			strcat(info, "\n - Unpack table is damaged");
		} else {
			strcat(info, "\n - Sub-blocks for next file :");
			for (i = 0; i < PACKTABLEENTR; i += 2) {
				pe = get_packed_address(i);
				if (pe < LOADADDRFILE3)
					pe = ENDADDRFILE3;
				if (pe > ENDADDRFILE3)
					pe = ENDADDRFILE3;
				if (pe > ps) {
					sprintf(lin, "\n   s-e: %04X-%04X", ps, pe);
					strcat(info, lin);
					if (pe == ENDADDRFILE3)
						break;
					ps = get_packed_address(i + 1);
				} else {
					break;
				}
			}
		}
#endif
	}
#endif //#if defined(DEBUGFFTABLES) || defined(DEBUGFFTABLESRAW)

	blk[row]->rd_err = rd_err;

	return(rd_err);
}
