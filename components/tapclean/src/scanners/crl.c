/*
 * crl.c (by Luigi Di Fraia, Jun 2017)
 *
 * Part of project "TAPClean". May be used in conjunction with "Final TAP".
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
 * CBM inspection needed: Yes
 * Single on tape: Commonly yes, but not necessarily (e.g. Frankenstein)
 * Sync: Custom pulses
 * Header: No
 * Data: Continuous
 * Checksum: No
 * Post-data: No
 * Trailer: No
 * Trailer homogeneous: N/A
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	CRL

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define MASTERLOADSIZE	0x300	/* max size in bytes of the first turbo file */

#ifdef _MSC_VER
#define inline __inline
#endif

#define CRL_DEBUG

static inline void get_crl_addresses (int *buf, int bufsz, int blkindex, unsigned int *s, unsigned int *x)
{
	int seq_next_file[] = {
		0xA9, XX, 0x8D, XX, XX, 0xA9, XX, 0x8D, XX, XX, 0xA9, XX, 0x85, 0x04, 0x20
	};
	int index, offset, deltaoffset, sumoffsets;

	deltaoffset = 0;
	sumoffsets = 0;

#ifdef CRL_DEBUG
	printf("\n---------------\nIndex: %d", blkindex);
#endif

	for (index = 0; index < blkindex; index++) {
		sumoffsets += deltaoffset;

		offset = find_seq(buf + sumoffsets, bufsz - sumoffsets, seq_next_file, sizeof(seq_next_file) / sizeof(seq_next_file[0]));
		if (offset == -1) {
#ifdef CRL_DEBUG
			printf("\nCould not find %d-th offset: no more files", index + 1);
#endif
			break;
		}

		sumoffsets += offset;
		deltaoffset = sizeof(seq_next_file) / sizeof(seq_next_file[0]);

#ifdef CRL_DEBUG
		printf("\nFound %d-th offset: %d", index + 1, sumoffsets);
#endif
	}

	if (offset == -1) {
		*s = 0;
		*x= 0;
#ifdef CRLEBUG
		printf("\nNo further file details found");
#endif
	} else {
		*s = buf[sumoffsets + 1] + 256 * buf[sumoffsets + 6];
		*x = 256 * buf[sumoffsets + 11];
#ifdef CRL_DEBUG
		printf("\nAbs offset: %d, s: $%04X, x: $%04X", sumoffsets, *s, *x);
#endif
	}
}

void crl_search (void)
{
	int i, j;			/* counters */
	int sof, sod, eod, eof;		/* file offsets */
	int ib;				/* condition variable */

	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int crl_index;			/* Index of file (0 = master loader) */

	int xinfo;			/* extra info used in addblockdef() */

	int masterloader[MASTERLOADSIZE];	/* Buffer for master loader (first turbo file, mostly 0x200 bytes but can be larger) */


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	if (!quiet)
		msgout("  CRL");

	crl_index = 0;

	/*
	 * First we retrieve the first file variables from the CBM data.
	 * We use CBM DATA index # 1 as we assume the tape image contains
	 * a single game.
	 * For compilations we should search and find the relevant file
	 * using the search code found e.g. in Biturbo.
	 *
	 */

	s = x = 0;

	ib = find_decode_block(CBM_HEAD, 1);
	if (ib != -1) {
		int *buf, bufsz;

		bufsz = blk[ib]->cx;

		buf = (int *) malloc (bufsz * sizeof(int));
		if (buf != NULL) {
			int seq_loader_setup[10] = {
				0xA2, XX, 0x86, 0x04, 0xA9, 0x06, 0x85, 0x05, 0x85, 0x01
			};

			int seq_start_address[9] = {
				0xAD, 0x0D, 0xDD, 0x4A, 0x7E, XX, XX, 0xC6, 0x02
			};

			int index, offset;

			/* Make an 'int' copy for use in find_seq() */
			for (index = 0; index < bufsz; index++)
				buf[index] = blk[ib]->dd[index];

			offset = find_seq(buf, bufsz, seq_loader_setup, sizeof(seq_loader_setup) / sizeof(seq_loader_setup[0]));
			if (offset != -1) {
				x = 256 * buf[offset + 1];
			}

			offset = find_seq(buf, bufsz, seq_start_address, sizeof(seq_start_address) / sizeof(seq_start_address[0]));
			if (offset != -1) {
				s = buf[offset + 5] + 256 * buf[offset + 6];
			}

			free (buf);
		}
	}
	
	/* Extraction successful? */
	if (s == 0 || x == 0)
		return;

	printf("  CRL masterload variables found and set: s=%04X, x=%04X\n", s, x);

	for (i = 20; i > 0 && i < tap.len - BITSINABYTE; i++) {

		/*
		 * Pilot pulses might end up being encoded as long pulses so we 
		 * can't just look for single byte pulses
		 */

		/*
		for (j = 0; j < 5; j++) {
			if (readttbit(i + j, 0xFD, 0x90, 0xC0) != 1)
				break;
		}
		if (j < 5) {
			i += j;
			continue;
		}
		*/

		/* Valid pilot found, mark start of file */
		//sof = i;
		//i += j;

		for (j = 0; j < 5; j++) {
			if (readttbit(i + j, 0xAD, 0x30, 0x60) != 1)
				break;
		}
		if (j < 5) {
			i += j;
			continue;
		}

		/* Valid sync found, mark start of data */
		sof = i;
		i += j;
		sod = i;

		/* Point to the first pulse of the last data byte (that's final) */
		eod = sod + (x - 1) * BITSINABYTE;

		/* Initially point to the last pulse of the last byte */
		eof = eod + BITSINABYTE - 1;

		/* Calculate end address */
		e = s + x - 1;

		/* Store the info read from CBM part as extra-info */
		xinfo = s + (e << 16);

		/* The last pulse is often missing/oversized */
		if (addblockdef(THISLOADER, sof, sod, eod, eof, xinfo) >= 0 || addblockdef(THISLOADER, sof, sod, eod, eof - 1, xinfo) >= 0) {
			i = eof;	/* Search for further files starting from the end of this one */
#ifdef CRL_DEBUG
			printf("\naddblockdef succeded for sof=%04X eof=%04X", sof, eof);
#endif

			if (crl_index == 0) {
				int b, rd_err;

				rd_err = 0;

				/* Store master loader (ignore last byte as it might be corrupted) */
				for (j = 0; j < (int)x - 1 && j < MASTERLOADSIZE; j++) {
					b = readttbyte(sod + (j  * BITSINABYTE), lp, sp, tp, en);
					if (b == -1)
						rd_err++; /* Don't break here: during debug we will see how many errors occur */
					else
						masterloader[j] = b;
				}
				for (; j < MASTERLOADSIZE; j++)
					masterloader[j] = 0xEA;	/* Padding with NOPs if required */

				if (rd_err == 0) {
#ifdef CRL_DEBUG
					printf("\nMasterload OK, start: $%04x", s);
#endif
					crl_index++;
					get_crl_addresses(masterloader, MASTERLOADSIZE, crl_index, &s, &x);
				} else  {
#ifdef CRL_DEBUG
					printf("\nMasterload FAIL, start: $%04x", s);
#endif
					/* We can't decode in case of error */
					crl_index = -1;
					s = x = 0;
				}
			} else if (crl_index > 0) {
				crl_index++;
				get_crl_addresses(masterloader, MASTERLOADSIZE, crl_index, &s, &x);
			}
		} else {
#ifdef CRL_DEBUG
			printf("\naddblockdef failed for sof=%04X eof=%04X", sof, eof);
#endif
		}

		/* More blocks to load? */
		if (s == 0 || x == 0)
			break;
	}
}

int crl_describe (int row)
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

	/* pilot is in pulses... */
	blk[row]->pilot_len = blk[row]->p2 - blk[row]->p1;

	/* ... trailer in pulses */
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync sequence (5 pulses) */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= 5;

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

	blk[row]->rd_err = rd_err;

	return(rd_err);
}
