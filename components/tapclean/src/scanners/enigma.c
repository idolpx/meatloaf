/*
 * enigma.c (Enigma Variations - rewritten by Luigi Di Fraia, May 2014)
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
 * CBM inspection needed: Yes
 * Single on tape: Yes
 * Sync: Byte
 * Header: No
 * Data: Continuous
 * Checksum: No
 * Post-data: No
 * Trailer: Commonly no, but not necessarily (e.g. DNA Warrior)
 * Trailer homogeneous: No
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	ENIGMA

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	1	/* amount of sync bytes */
#define MAXTRAILER	0x800	/* max amount of trailer pulses read in (see ACE 2 trailing leftovers) */

#define MASTERLOADSIZE	0x200	/* max size in bytes of the first turbo file */

#ifdef _MSC_VER
#define inline __inline
#endif

//#define ENIGMA_DEBUG

static inline void get_enigma_addresses (int *buf, int bufsz, int entrypointoffset, int blkindex, unsigned int *s, unsigned int *e)
{
	/* Load snippets usually found in the master loader */
	int seq_load_type1[15] = {
		/*
		 * Example from Defenders of the Earth:
		 * LDA #$00	; Load address LSB
		 * STA $60
		 * LDA #$E0	; MSB of the same
		 * STA $61
		 * LDA #$40	; End address LSB
		 * LDY #$FF	; MSB of the same
		 * JSR $02BA	; Load
		 *
		 * Example from Implosion (same as ACE 2):
		 * LDA #$00	; Load address LSB
		 * STA $F8
		 * LDA #$F0	; MSB of the same
		 * STA $F9
		 * LDA #$FA	; End address LSB
		 * LDY #$FF
		 * JSR $02BF	; Load
		 */
		0xA9,XX,0x85,XX,0xA9,XX,0x85,XX,0xA9,XX,0xA0,XX,0x20,XX,0x02
	};
	int seq_load_type2[15] = {
		/*
		 * Example from Defenders of the Earth:
		 * LDA #$00	; Load address LSB
		 * LDX #$CC	; MSB of the same
		 * STA $60
		 * STX $61
		 * LDA #$00	; End address LSB
		 * LDY #$D0	; MSB of the same
		 * JSR $02BA	; Load
		 *
		 * Example from The Famous Five:
		 * LDA #$00	; Load address LSB
		 * LDX #$CC	; MSB of the same
		 * STA $60
		 * STX $61
		 * LDA #$00	; End address LSB
		 * LDY #$D0	; MSB of the same
		 * JSR $02BC	; Load
		 */
		0xA9,XX,0xA2,XX,0x85,XX,0x86,XX,0xA9,XX,0xA0,XX,0x20,XX,0x02
	};

	int index, offset1, offset2, minoffset, deltaoffset, sumoffsets;

	index = 1;
	minoffset = 0;
	deltaoffset = 0;
	sumoffsets = entrypointoffset;

#ifdef ENIGMA_DEBUG
	printf ("\n---------------\nIndex: %d", blkindex);
#endif

	do {
		sumoffsets += (minoffset + deltaoffset);

		offset1 = find_seq(buf + sumoffsets, bufsz - sumoffsets, seq_load_type1, sizeof(seq_load_type1) / sizeof(seq_load_type1[0]));
		offset2 = find_seq(buf + sumoffsets, bufsz - sumoffsets, seq_load_type2, sizeof(seq_load_type2) / sizeof(seq_load_type2[0]));

#ifdef ENIGMA_DEBUG
		printf ("\nScanning for seq at: %d, ofst1 = %d, ofst2 = %d", sumoffsets, offset1, offset2);
#endif

		if (offset1 >= 0 && (offset1 < offset2 || offset2 == -1)) {
			minoffset = offset1;
			deltaoffset = sizeof(seq_load_type1) / sizeof(seq_load_type1[0]);
		}
		if (offset2 >= 0 && (offset2 < offset1 || offset1 == -1)) {
			minoffset = offset2;
			deltaoffset = sizeof(seq_load_type2) / sizeof(seq_load_type2[0]);
		}

#ifdef ENIGMA_DEBUG
		printf ("\nmin offset = %d, skip next: %d", minoffset, deltaoffset);
#endif

		index++;
	} while ((index <= blkindex) && !(offset1 == -1 && offset2 == -1));

	if (offset1 == -1 && offset2 == -1) {
		*s = 0;
		*e = 0;
#ifdef ENIGMA_DEBUG
		printf ("\nNo further file details found");
#endif
	} else {
		*s  = buf[sumoffsets + minoffset + 1];

		if (minoffset == offset1) {
			*s |= buf[sumoffsets + minoffset + 5] << 8;
		} else {
			*s |= buf[sumoffsets + minoffset + 3] << 8;
		}
		*e  = buf[sumoffsets + minoffset + 9];
		*e |= buf[sumoffsets + minoffset + 11] << 8;

#ifdef ENIGMA_DEBUG
		printf ("\ns: $%04X, e: $%04X", *s, *e);
#endif
	}
}

void enigma_search(void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int ib;				/* condition variable */

	int en, tp, sp, lp, sv;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int enigma_index;		/* Index of file (0 = master loader) */

	int xinfo;			/* extra info used in addblockdef() */

	int masterloader[MASTERLOADSIZE];	/* Buffer for master loader (first turbo file, mostly 0x200 bytes) */
	int masterentryvectoroffset, masterentryoffset;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;
	sv = ft[THISLOADER].sv;

	if (!quiet)
		msgout("  Enigma Variations Tape");

	enigma_index = 0;

	/*
	 * First we retrieve the first file variables from the CBM data.
	 * We use CBM DATA index # 1 as we assume the tape image contains
	 * a single game.
	 * For compilations we should search and find the relevant file
	 * using the search code found e.g. in Biturbo.
	 *
	 * Note: if we can't get these variables we still fall back to the
	 *       "brute read" method.
	 */

	s = e = 0;	/* Assume we were unable to extract any info */

	ib = find_decode_block(CBM_DATA, 1);
	if (ib != -1) {
		int *buf, bufsz;

		bufsz = blk[ib]->cx;

		buf = (int *) malloc (bufsz * sizeof(int));
		if (buf != NULL) {
			/*
			 * Example from Defenders of the Earth:
			 * JSR $03C4
			 * LDA #$00	; First file load address LSB
			 * STA $60
			 * LDY #$C0	; MSB of the same
			 * STY $61
			 * INY		; End address is 2 pages ahead (0x200 bytes)
			 * INY
			 * JSR S02BA
			 * JMP ($0910)	; Game specific jump address
			 *
			 * Example from Implosion (Not yet fully supported):
			 * LDA #$39      
			 * STA $0328
			 * JSR $03C4
			 * LDA #$00
			 * STA $F8
			 * LDY #$0C
			 * STY $F9
			 * INY
			 * INY
			 * JSR S02BF
			 * JMP ($0C00)	; Game specific jump address
			 *
			 * Example from Frightmare:
			 * JSR $03C4
			 * LDA #$00
			 * STA $60
			 * LDY #$08
			 * STY $61
			 * INY
			 * INY
			 * JSR S02BA
			 * JMP $0810	; Note: Direct jump
			 */
			int seq_load_addr_and_exec_first1[19] = {
				/* Whole sequence that we will need so array access later on is safe */
				0x20, 0xC4, 0x03, 0xA9, XX, 0x85, XX, 0xA0, XX, 0x84, XX, 
				0xC8, 0xC8, 0x20, XX, 0x02, XX, XX, XX
			};

			/*
			 * Example from ACE 2:
			 * LDA #$39
			 * STA $0328
			 * JSR $03C7
			 * LDA #$00	; First file load address LSB
			 * STA $F8
			 * LDY #$CC	; MSB of the same
			 * STY $F9
			 * INY		; Note: End address is only 1 page ahead (0x100 bytes)
			 * JSR S02C0
			 * LDY #$00
			 * JMP ($CC00)	; Game specific jump address
			 */
			int seq_load_addr_and_exec_first2[20] = {
				/* Whole sequence that we will need so array access later on is safe */
				0x20, 0xC7, 0x03, 0xA9, XX, 0x85, XX, 0xA0, XX, 0x84, XX, 
				0xC8, 0x20, XX, 0x02, 0xA0, XX, 0x6C, XX, XX
			};

			/*
			 *
			 * Example from The Famous Five (Not yet supported)
			 * JSR $03C4
			 * LDA #$00
			 * STA $60
			 * LDY #$04
			 * STY $61
			 * LDA #$C5	; Explicitly set End address
			 * LDY #$04
			 * JSR S02BC
			 * JMP ($0400)
			 */
			int seq_load_addr_and_exec_first3[21] = {
				/* Whole sequence that we will need so array access later on is safe */
				0x20, 0xC4, 0x03, 0xA9, XX, 0x85, XX, 0xA0, XX, 0x84, XX, 
				0xA9, XX, 0xA0, XX, 0x20, XX, 0x02, 0x6C, XX, XX
			};

			int index, offset;

			/* Make an 'int' copy for use in find_seq() */
			for (index = 0; index < bufsz; index++)
				buf[index] = blk[ib]->dd[index];

			offset = find_seq(buf, bufsz, seq_load_addr_and_exec_first1, sizeof(seq_load_addr_and_exec_first1) / sizeof(seq_load_addr_and_exec_first1[0]));
			if (offset != -1) {
				/* Update s and e for the first block if info was found */
				s  = buf[offset + 4];
				s |= buf[offset + 8] << 8;
				e = s + 0x200;

				switch (buf[offset + 16]) {
					case 0x6C:	/* JMP ($0000): We need to de-reference later */
						masterentryvectoroffset = (buf[offset + 17] | (buf[offset + 18] << 8)) - s;

						/* Plausibility check as we work with integers */
						if (masterentryvectoroffset < 0)
							s = e = 0;
#ifdef ENIGMA_DEBUG
						else
							printf ("\nMasterload entry vector: $%04x", masterentryvectoroffset + s);
#endif
						break;
					case 0x4C:	/* JMP $0000: No need to de-reference */
						masterentryvectoroffset = -1;
						masterentryoffset = (buf[offset + 17] | (buf[offset + 18] << 8)) - s;
#ifdef ENIGMA_DEBUG
						printf ("\nMasterload start: $%04x", masterentryoffset + s);
#endif
						break;
				}
			}

			offset = find_seq(buf, bufsz, seq_load_addr_and_exec_first2, sizeof(seq_load_addr_and_exec_first2) / sizeof(seq_load_addr_and_exec_first2[0]));
			if (offset != -1) {
				/* Update s and e for the first block if info was found */
				s  = buf[offset + 4];
				s |= buf[offset + 8] << 8;
				e = s + 0x100;

				masterentryvectoroffset = (buf[offset + 18] | (buf[offset + 19] << 8)) - s;

				/* Plausibility check as we work with integers */
				if (masterentryvectoroffset < 0)
					s = e = 0;
#ifdef ENIGMA_DEBUG
				else
					printf ("\nMasterload entry vector: $%04x", masterentryvectoroffset + s);
#endif
			}

			offset = find_seq(buf, bufsz, seq_load_addr_and_exec_first3, sizeof(seq_load_addr_and_exec_first3) / sizeof(seq_load_addr_and_exec_first3[0]));
			if (offset != -1) {
				/* Update s and e for the first block if info was found */
				s  = buf[offset + 4];
				s |= buf[offset + 8] << 8;
				e  = buf[offset + 12];
				e |= buf[offset + 14] << 8;

				masterentryvectoroffset = (buf[offset + 19] | (buf[offset + 20] << 8)) - s;

				/* Plausibility check as we work with integers */
				if (masterentryvectoroffset < 0)
					s = e = 0;
#ifdef ENIGMA_DEBUG
				else
					printf ("\nMasterload entry vector: $%04x", masterentryvectoroffset + s);
#endif
			}

			free (buf);
		}
	}

	/* Rik The Roadie: the first file is 3 pages, only 2 of which are loaded */
	if (ib != -1 && s && e == s + 0x200) {
		unsigned int crc;

		/*
		 * At this stage the describe functions have not been invoked
		 * yet, therefore we have to compute the CRC-32 on the fly.
		 */
		crc = crc32_compute_crc(blk[ib]->dd, blk[ib]->cx);

		if (crc == 0xAA370E0D)
			e += 0x100;
	}

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

			if (s == 0) {
				int b;

				/* scan through all readable bytes... */
				do {
					b = readttbyte(i, lp, sp, tp, en);
					if (b != -1)
						i += BITSINABYTE;
				} while (b != -1);

				/* Point to the first pulse of the last data byte (that's final) */
				eod = i - BITSINABYTE;

				/* Point to the last pulse of the last byte */
				eof = eod + BITSINABYTE - 1;

				if (addblockdef(THISLOADER, sof, sod, eod, eof, 0) >= 0)
					i = eof;	/* Search for further files starting from the end of this one */
			} else {
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

				/* Point to the last pulse of the last byte */
				eof = eod + BITSINABYTE - 1;

				/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
				/* Note: only seen in DNA Warrior */
				h = 0;
				while (eof < tap.len - 1 &&
						h++ < MAXTRAILER &&
						readttbit(eof + 1, lp, sp, tp) >= 0)
					eof++;

				/* Store the info read from master loader as extra-info */
				xinfo = s + (e << 16);

				if (addblockdef(THISLOADER, sof, sod, eod, eof, xinfo) >= 0) {
					i = eof;	/* Search for further files starting from the end of this one */

					if (enigma_index == 0) {
						int j, b, rd_err;

						rd_err = 0;

						/* Store master loader */
						for (j = 0; j < (int)x && j < MASTERLOADSIZE; j++) {
							b = readttbyte(sod + (j  * BITSINABYTE), lp, sp, tp, en);
							if (b == -1)
								rd_err++; /* Don't break here: during debug we will see how many errors occur */
							else
								masterloader[j] = b;
						}
						for (; j < MASTERLOADSIZE; j++)
							masterloader[j] = 0xEA;	/* Padding with NOPs if required */

						if (rd_err == 0) {
							enigma_index++;

							if (masterentryvectoroffset != -1) {
								if (masterentryvectoroffset < (int)x - 2 /* We need to read 2 bytes */) {
									/* Dereference jump in masterloader if we need to and can do */
									masterentryoffset = (masterloader[masterentryvectoroffset] | (masterloader[masterentryvectoroffset + 1] << 8)) - s;
#ifdef ENIGMA_DEBUG
									printf ("\nMasterload start: $%04x", masterentryoffset + s);
#endif
								} else {
									/* Make the next check fail */
									masterentryoffset = -1;
								}
							}

							if (masterentryoffset < 0 || masterentryoffset >= (int)x) {
								/* Offset is negative or past the end of the file, give up without trying extraction */
								enigma_index = -1;
								s = e = 0;
							} else {
								get_enigma_addresses (masterloader, MASTERLOADSIZE, masterentryoffset, enigma_index, &s, &e);
							}
						} else {
							/* We can't decode in case of error */
							enigma_index = -1;
							s = e = 0;
						}
					} else if (enigma_index > 0) {
						enigma_index++;
						get_enigma_addresses (masterloader, MASTERLOADSIZE, masterentryoffset, enigma_index, &s, &e);
					}
				}
			}
		}
	}
}


int enigma_describe(int row)
{
	int i, s;
	int en, tp, sp, lp;

	int b, rd_err;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	if (blk[row]->xi != 0) {
		/* Retrieve C64 memory location for load/end address from extra-info */
		blk[row]->cs = blk[row]->xi & 0xFFFF;
		blk[row]->ce = (blk[row]->xi & 0xFFFF0000) >> 16;
		blk[row]->cx = (blk[row]->ce - blk[row]->cs) + 1;

		/* Compute pilot & trailer lengths */

		/* pilot is in bytes... */
		blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / BITSINABYTE;

		/* ... trailer in pulses */
		blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

		/* if there IS pilot then disclude the sync sequence */
		if (blk[row]->pilot_len > 0)
			blk[row]->pilot_len -= SYNCSEQSIZE;

	} else {
		/* compute data size... */
		blk[row]->cx = ((blk[row]->p3 - blk[row]->p2) >> 3) + 1;
		blk[row]->cs = 0;			/* fake start address */
		blk[row]->ce = blk[row]->cx - 1;	/* fake end   address */

		/* get pilot & trailer length */
		blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1 - 8) >> 3;
		blk[row]->trail_len = 0;
	}

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

