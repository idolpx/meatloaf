/*
 * creatures.c (by Luigi Di Fraia, Aug 2006)
 * Based on cyberload_f4.c, turrican.c
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
 * Note: Do not copy code from this scanner because it implements a block info
 *       lookup based on known values from what's thought to be a healthy 
 *       original version.
 *
 * CBM inspection needed: No
 * Single on tape: No
 * Sync: Byte
 * Header: Yes
 * Data: Continuous
 * Checksum: No
 * Post-data: No
 * Trailer: Yes
 * Trailer homogeneous: No
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	CREATURES

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	1	/* amount of sync bytes */
#define MAXTRAILER	8	/* max amount of trailer pulses read in */

#define HEADERSIZE	1	/* size of block header */

#define BLKNUMOFFSET	0	/* block number offset inside header */

struct creatures_level_table_s {
	unsigned char id;	/* file ID */
	unsigned int s;		/* load address of block */
	unsigned int e;		/* end address + 1 */
	int checkbyte;		/* pre-calculated XOR checkbytes (not part of the game/loader) */
};

/*
 * Table for level files. Larger blocks at top so that upon a failure of 
 * addblockdef, the next smaller block is attempted.
 */
struct creatures_level_table_s creatures_level_table[] = {
	/* Torture Trouble - Creatures 2 */
	{0x31 ^ 0xFF, 0x5800, 0xB000, 0xBD},
	{0x32 ^ 0xFF, 0x5800, 0xB000, 0xC8},
	{0x33 ^ 0xFF, 0x5800, 0xB000, 0x21},
	{0x34 ^ 0xFF, 0x5800, 0xB000, 0x42},
	{0x35 ^ 0xFF, 0x5800, 0xB000, 0x55},
	{0x36 ^ 0xFF, 0x5800, 0xB000, 0xFA},
	{0x37 ^ 0xFF, 0x5800, 0xB000, 0xF4},
	{0x38 ^ 0xFF, 0x5800, 0xB000, 0x63},
	{0x39 ^ 0xFF, 0x5800, 0xB000, 0xCB},
	{0x41 ^ 0xFF, 0x5800, 0xB000, 0x78},
	{0x42 ^ 0xFF, 0x5800, 0xB000, 0xDE},
	{0x43 ^ 0xFF, 0x5800, 0xB000, 0x41},

	/* Mayhem in Monsterland */
	{0x31 ^ 0xFF, 0x5A00, 0xB100, 0x3C},
	{0x32 ^ 0xFF, 0x5A00, 0xB100, 0x0C},
	{0x33 ^ 0xFF, 0x5A00, 0xB100, 0xC9},
	{0x34 ^ 0xFF, 0x5A00, 0xB100, 0x53},
	{0x35 ^ 0xFF, 0x5A00, 0xB100, 0xAD},
	{0x43 ^ 0xFF, 0x5A00, 0xB100, 0x17},

	/* Creatures */
	{0x54 ^ 0xFF, 0x5800, 0x6A00, 0xA7},
	{0x31 ^ 0xFF, 0x5800, 0xAC00, 0x28},
	{0x32 ^ 0xFF, 0x8700, 0xAC00, 0x96},
	{0x33 ^ 0xFF, 0x5800, 0xAC00, 0xB1},
	{0x34 ^ 0xFF, 0x5800, 0xAC00, 0x1D},
	{0x35 ^ 0xFF, 0x8700, 0x9A00, 0x1B},
	{0x36 ^ 0xFF, 0x5800, 0xAC00, 0xDD},
	{0x37 ^ 0xFF, 0x5800, 0xAC00, 0x82},
	{0x38 ^ 0xFF, 0x8700, 0x9A00, 0xD3},
	{0x39 ^ 0xFF, 0x5800, 0xAC00, 0x8D},
//	{0x43 ^ 0xFF, 0x5800, 0x8D00, 0x??} // this refers to what the loader loads from tape
	{0x43 ^ 0xFF, 0x5800, 0xAC00, 0x60} // this refers to the block actually on tape
};
#define CREATURES_TABLE_SIZE (sizeof(creatures_level_table)/sizeof(creatures_level_table[0]))

void creatures_search (void)
{
	int i, h, j;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */

	int en, tp, sp, lp, sv;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int xinfo;			/* extra info used in addblockdef() */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;
	sv = ft[THISLOADER].sv;

	if (!quiet)
		msgout("  Creatures");

	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {
		eop = find_pilot(i, THISLOADER);

		if (eop > 0) {
			/* Valid pilot found, mark start of file */
			sof = i;
			i = eop;

			/* Check if there's a valid sync byte for this loader */
			if (readttbyte(i, lp, sp, tp, en) != sv)
				continue;

			/* Valid sync found, mark start of data */
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

			/* Lookup load and end addresses from level table by matching ID */
			for (j = 0; j < CREATURES_TABLE_SIZE; j++) {
				if (hd[BLKNUMOFFSET] == creatures_level_table[j].id) {
					s = creatures_level_table[j].s;
					e = creatures_level_table[j].e;

					/* Save file id offset for describe stage */
					xinfo = j;

					/* Prevent int wraparound when subtracting 1 from end location
					   to get the location of the last loaded byte */
					if (e == 0)
						e = 0xFFFF;
					else
						e--;

					/* Compute size */
					x = e - s + 1;

					/* Point to the first pulse of the last data byte (that's final) */
					eod = sod + (HEADERSIZE + x - 1) * BITSINABYTE;

					/* Initially point to the last pulse of the last byte */
					eof = eod + BITSINABYTE - 1;

					/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
					h = 0;
					while (eof < tap.len - 1 &&
							h++ < MAXTRAILER &&
							readttbit(eof + 1, lp, sp, tp) >= 0)
						eof++;

					if (addblockdef(THISLOADER, sof, sod, eod, eof, xinfo) >= 0)
						i = eof;	/* Search for further files starting from the end of this one */
				}
			}

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int creatures_describe (int row)
{
	int i, s;
	int index;		/* table index */
	int en, tp, sp, lp;
	int cb;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Retrieve file index */
	index = blk[row]->xi;

	/* Set filename to file ID */
	if (blk[row]->fn != NULL)
		free(blk[row]->fn);
	blk[row]->fn = (char*)malloc(2);
	sprintf(blk[row]->fn, "%c", creatures_level_table[index].id ^ 0xFF);

	/* Lookup other details from level table */
	blk[row]->cs = creatures_level_table[index].s;
	blk[row]->ce = creatures_level_table[index].e;

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

	/* Extract data and test checksum... */
	rd_err = 0;
	cb = 0;

	s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);

	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

	for (i = 0; i < blk[row]->cx; i++) {
		b = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, en);
		b ^= 0xFF;	/* all bits are inverted on tape */
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

	blk[row]->cs_exp = cb & 0xFF;
	blk[row]->cs_act = creatures_level_table[index].checkbyte & 0xFF;
	blk[row]->rd_err = rd_err;

	return(rd_err);
}
