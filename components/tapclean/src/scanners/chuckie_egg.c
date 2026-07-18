/*
 * chuckie_egg.c (by Luigi Di Fraia, Oct 2009)
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
 * Sync: Byte (increasing values, starting from 0x00)
 * Header: Yes
 * Data: Continuous
 * Checksum: Yes (inside header)
 * Post-data: No
 * Trailer: Yes
 * Trailer homogeneous: No
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	CHUCKIEEGG

#define BITSINABYTE	9	/* a byte is made up of 9 bits here */

#define SYNCSEQSIZE	1	/* amount of sync bytes */
#define MAXTRAILER	90	/* max amount of trailer pulses read in */

#define HEADERSIZE	9	/* size of block header */

#define LOADOFFSETH	7	/* load location (MSB) offset inside header */
#define LOADOFFSETL	8	/* load location (LSB) offset inside header */
#define DATAOFFSETH	5	/* data size (MSB) offset inside header */
#define DATAOFFSETL	6	/* data size (LSB) offset inside header */
#define CRCOFFSERH	1	/* CRC-16 (MSB) offset inside header */
#define CRCOFFSERL	2	/* CRC-16 (LSB) offset inside header */

#ifdef _MSC_VER
#define inline __inline
#endif

/* This is the CRC used by the Xmodem-CRC protocol */
static inline unsigned int crc_xmodem_update (unsigned int crc, unsigned char data)
{
	int i;

	crc ^= ((unsigned int)data << 8);
	for (i = 0; i < 8; i++) {
		if (crc & 0x8000)
			crc = (crc << 1) ^ 0x1021;
		else
			crc <<= 1;
	}

	return (crc & 0xFFFF);
}

int chuckieegg_readbyte(int pos, int lp, int sp, int tp, int endi)
{
	int i, v, b;
	unsigned char byte[8];

	/* check next 8 pulses are not inside a pause and *are* inside the TAP... */

	for(i = 0; i < 8; i++) {
		b = readttbit(pos + i, lp, sp, tp);
		if (b == -1)
			return -1;
		else
			byte[i] = b;
	}

	/* check sync mark (bit 1) */

	b = readttbit(pos + i, lp, sp, tp);
	if (b != 1)
		return -1;

	/* if we got this far, we have 8 valid bits... decode the byte... */

	if (endi == MSbF) {
		v = 0;
		for (i = 0; i < 8; i++) {
			if (byte[i] == 1)
				v += (128 >> i);
		}
	} else {
		v = 0;
		for (i = 0; i < 8; i++) {
			if (byte[i] == 1)
			v += (1 << i);
		}
	}

	/* Invert all bits before returning the value since this loader uses
	 * bit 0 pulse longer than bit 1
	 */
	return (v ^ 0xFF);
}

void chuckieegg_search (void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */

	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	if (!quiet)
		msgout("  Chuckie Egg");

	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {
		eop = find_pilot_bytes_ex(i, THISLOADER, chuckieegg_readbyte, BITSINABYTE);

		if (eop > 0) {
			/* Valid pilot found, mark start of file */
			sof = i;
			i = eop;

			/* Check if there's a valid sync byte for this loader */
			if (chuckieegg_readbyte(i, lp, sp, tp, en) < 0)
				continue;

			/* Valid sync found, mark start of data */
			sod = i + SYNCSEQSIZE * BITSINABYTE;

			/* Read header */
			for (h = 0; h < HEADERSIZE; h++) {
				hd[h] = chuckieegg_readbyte(sod + h * BITSINABYTE, lp, sp, tp, en);
				if (hd[h] == -1)
					break;
			}

			/* Bail out if there was an error reading the block header */
			if (h != HEADERSIZE)
				continue;

			/* Extract load location and size */
			s = hd[LOADOFFSETL] + (hd[LOADOFFSETH] << 8);
			x = hd[DATAOFFSETL] + (hd[DATAOFFSETH] << 8) + 1;

			/* Compute C64 memory location of the _LAST loaded byte_ */
			e = s + x - 1;

			/* Plausibility check */
			if (e > 0xFFFF)
				continue;

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

			if (addblockdef(THISLOADER, sof, sod, eod, eof, 0) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int chuckieegg_describe(int row)
{
	int i, s;
	int hd[HEADERSIZE];
	int en, tp, sp, lp;
	int crc;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	/* Set read pointer to the beginning of the payload */
	s = blk[row]->p2;

	/* Read header (it's safe to read it here for it was already decoded during the search stage) */
	for (i = 0; i < HEADERSIZE; i++)
		hd[i] = chuckieegg_readbyte(s + i * BITSINABYTE, lp, sp, tp, en);

	/* Read/compute C64 memory location for load/end address, and read data size */
	blk[row]->cs = hd[LOADOFFSETL] + (hd[LOADOFFSETH] << 8);
	blk[row]->cx = hd[DATAOFFSETL] + (hd[DATAOFFSETH] << 8) + 1;

	/* C64 memory location of the _LAST loaded byte_ */
	blk[row]->ce = blk[row]->cs + blk[row]->cx - 1;

	/* Compute pilot & trailer lengths */

	/* pilot is in bytes... */
	blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / BITSINABYTE;

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync byte */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= SYNCSEQSIZE;

	/* Extract data and test checksum... */
	rd_err = 0;
	crc = 0;

	s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);

	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

	for (i = 0; i < blk[row]->cx; i++) {
		b = chuckieegg_readbyte(s + (i * BITSINABYTE), lp, sp, tp, en);

#ifdef FORCECHUCKIEEGGCRC
		/* When part 3 checksum is calculated by the game, the contents
		 * in RAM have changed since they were loaded. We change them
		 * too to end up with the expected CRC.
		 */
		if (blk[row]->cs + i == 0x0A76)
			crc = crc_xmodem_update(crc, 0xFD);
		else
			crc = crc_xmodem_update(crc, b);
#else
		crc = crc_xmodem_update(crc, b);
#endif

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

	blk[row]->cs_exp = crc;
	blk[row]->cs_act = hd[CRCOFFSERL] + (hd[CRCOFFSERH] << 8);
	blk[row]->rd_err = rd_err;

	return(rd_err);
}
