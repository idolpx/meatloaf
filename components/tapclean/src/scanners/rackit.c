/*
 * rackit.c (Rack-it/Hewson - rewritten by Luigi Di Fraia, June 2014)
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
 * Header: Yes
 * Data: Continuous
 * Checksum: Yes
 * Post-data: No
 * Trailer: Yes
 * Trailer homogeneous: Yes (bit 1 pulses)
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	RACKIT

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	1	/* amount of sync bytes */
#define MAXTRAILER	8	/* max amount of trailer pulses read in */

/* For the version that was already supported in Final TAP */
#define HEADERSIZE	8	/* size of block header */

#define XOROFFSET	0	/* xor cypher value (for data) inside header */
#define BANKOFFSET	1	/* value for $01 */
#define CHKBOFFSET	2	/* check byte offset inside header */
#define ENDOFFSETH	3	/* end  location (MSB) offset inside header */
#define ENDOFFSETL	4	/* end  location (LSB) offset inside header */
#define LOADOFFSETH	5	/* load location (MSB) offset inside header */
#define LOADOFFSETL	6	/* load location (LSB) offset inside header */
#define BLKNUMOFFSET	7	/* block number offset inside header */

/* For the variant that uses a 7-byte header (Marauder, Netherworld, Scorpion) */
#define HEADERSIZEV1	7	/* size of block header */

#define BANKOFFSETV1	0	/* value for $01 */
#define CHKBOFFSETV1	1	/* check byte offset inside header */
#define ENDOFFSETHV1	2	/* end  location (MSB) offset inside header */
#define ENDOFFSETLV1	3	/* end  location (LSB) offset inside header */
#define LOADOFFSETHV1	4	/* load location (MSB) offset inside header */
#define LOADOFFSETLV1	5	/* load location (LSB) offset inside header */
#define BLKNUMOFFSETV1	6	/* block number offset inside header */

#define RACKIT_CBM_DATA_LOAD_ADDRESS	0x0316

//#define DEBUG_RACKIT

void rackit_search (void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */
	int ib;				/* condition variable */

	int en, tp, sp, lp, sv;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int cypher_value;		/* XOR value to decode the core loader */

	int variant;			/* variant of the loader (8 or 7 header bytes) */

	int xinfo;			/* extra info used in addblockdef() */


	if (!quiet)
		msgout("  Rack-It tape (+Variant)");

	/*
	 * First we retrieve loader variables from the CBM data block, 
	 * after decrypting it.
	 * We use CBM DATA index # 1 as we assume the tape image contains
	 * a single game.
	 * For compilations we should search and find the relevant file
	 * using the search code found e.g. in Biturbo.
	 */

	cypher_value = -1;	/* Assume we were unable to extract any info */

	/*
	 * At this stage the describe functions have not been invoked yet,
	 * therefore we have to extract the CBM Data load address on the fly.
	 *
	 * The CBM Data load address is stored in CBM Header so we need to
	 * decode both before we can access the blk[ib]->cs record of the
	 * corresponding CBM Data.
	 */
	find_decode_block(CBM_HEAD, 1);	/* As per comment above, we don't need its index */
	ib = find_decode_block(CBM_DATA, 1);	/* This fails if the above failed */
	if (ib != -1 && (unsigned int) blk[ib]->cs == RACKIT_CBM_DATA_LOAD_ADDRESS) {
		int *buf, bufsz;

		bufsz = blk[ib]->cx;

		buf = (int *) malloc (bufsz * sizeof(int));
		if (buf != NULL) {
			/*
			 * Example from Zamzara:
			 * 
			 * *=$046F   
			 * LDA $7C
			 * EOR $04BD,X
			 * STA $0200,X
			 */
			int seq_decrypt1[8] = {
				0xA5, XX, 0x5D, XX, 0x04, 0x9D, 0x00, 0x02
			};

			/*
			 * Example from Netherworld:
			 * 
			 * *=$0465
			 * LDA #$B4
			 * EOR $04BD,X
			 * STA $0200,X
			 */
			int seq_decrypt2[8] = {
				0xA9, XX, 0x5D, XX, 0x04, 0x9D, 0x00, 0x02
			};

			int index, offset;

			/* Make an 'int' copy for use in find_seq() */
			for (index = 0; index < bufsz; index++)
				buf[index] = blk[ib]->dd[index];

			offset = find_seq(buf, bufsz, seq_decrypt1, sizeof(seq_decrypt1) / sizeof(seq_decrypt1[0]));
			if (offset != -1) {
				switch (buf[offset + 1]) {
					case 0x79:
						cypher_value = 0xB4;
						break;
					case 0x7C:
						cypher_value = 0x24;
						break;

					/* We might find more ZP registers used in future */
				}

#ifdef DEBUG_RACKIT
				printf ("\nCypher value: %02X", cypher_value);
#endif
			}

			offset = find_seq(buf, bufsz, seq_decrypt2, sizeof(seq_decrypt2) / sizeof(seq_decrypt2[0]));
			if (offset != -1) {
				cypher_value = buf[offset + 1];

#ifdef DEBUG_RACKIT
				printf ("\nCypher value: %02X", cypher_value);
#endif
			}

			/* Decrypt the main loader if we found the cypher value */
			if (cypher_value != -1) {
				int seq_endianness[9] = {
					0x4E, 0x0D, 0xDD, 0xA9, 0x19, 0x8D, 0x0E, 0xDD, XX
				};

				int seq_pilot_sync[8] = {
					0xC9, XX, 0xF0, XX, 0xC9, XX, 0xD0, XX
				};

				/*
				 * Example from 5th Gear:
				 *
				 * DEX
				 * STX $FC
				 */
				int seq_inital_xor_value[3] = {
					0xCA, 0x86, 0xFC
				};

				int seq_hdr_store[2] = {
					0x95, 0xF3
				};

				/* Decrypt to the whole lot, even if it's only applicable starting at $04BD */
				for (index = 0; index < bufsz; index++)
					buf[index] ^= cypher_value;

				/* We now look for invariants to extract loader parameters */
				offset = find_seq(buf, bufsz, seq_endianness, sizeof(seq_endianness) / sizeof(seq_endianness[0]));
				if (offset == -1) {
					cypher_value = -1;	/* Fail the extraction */
				} else {
					ft[THISLOADER].en = (buf[offset + 8] == OPC_ROL) ? MSbF : LSbF;
				}

				offset = find_seq(buf, bufsz, seq_pilot_sync, sizeof(seq_pilot_sync) / sizeof(seq_pilot_sync[0]));
				if (offset == -1) {
					cypher_value = -1;	/* Fail the extraction */
				} else {
					ft[THISLOADER].pv = buf[offset + 1];
					ft[THISLOADER].sv = buf[offset + 5];
					
					/* Check what the start value of the Data checkbyte should be */
					offset = find_seq(buf, bufsz, seq_inital_xor_value, sizeof(seq_inital_xor_value) / sizeof(seq_inital_xor_value[0]));
					if (offset == -1)
						xinfo = 0x80;
					else
						xinfo = 0x00;
#ifdef DEBUG_RACKIT
					printf ("\nInitial checkbyte value: %02X", xinfo);
#endif
					/* Check if the variant with a shorter header is used (Marauder, Netherworld, Scorpion) */
					offset = find_seq(buf, bufsz, seq_hdr_store, sizeof(seq_hdr_store) / sizeof(seq_hdr_store[0]));
					if (offset == -1)
						variant = 0;
					else
						variant = 1;

					xinfo |= variant << 8;
				}
			}

			free (buf);
		}
	}

	/* Decryption and extraction successful? */
	if (cypher_value == -1)
		return;

	sprintf(lin,
		"  Rack-It variables found and set: pv=$%02X, sv=$%02X, en=%s", 
		ft[THISLOADER].pv, 
		ft[THISLOADER].sv, 
		ENDIANNESS_TO_STRING(ft[THISLOADER].en));
	msgout(lin);

	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;
	sv = ft[THISLOADER].sv;

	if (variant == 0) {
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

				/* Point to the first pulse of the last data byte (that's final) */
				eod = sod + (HEADERSIZE + x - 1) * BITSINABYTE;

				/* Point to the last pulse of the last byte */
				eof = eod + BITSINABYTE - 1;

				/* Trace 'eof' to end of trailer (bit 1 pulses only) */
				h = 0;
				while (eof < tap.len - 1 &&
						h++ < MAXTRAILER &&
						readttbit(eof + 1, lp, sp, tp) == 1)
					eof++;

				if (addblockdef(THISLOADER, sof, sod, eod, eof, xinfo) >= 0)
					i = eof;	/* Search for further files starting from the end of this one */

			} else {
				if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
					i = (-eop);
			}
		}
	} else {
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
				for (h = 0; h < HEADERSIZEV1; h++) {
					hd[h] = readttbyte(sod + h * BITSINABYTE, lp, sp, tp, en);
					if (hd[h] == -1)
						break;
				}
				if (h != HEADERSIZEV1)
					continue;

				/* Extract load and end locations */
				s = hd[LOADOFFSETLV1] + (hd[LOADOFFSETHV1] << 8);
				e = hd[ENDOFFSETLV1]  + (hd[ENDOFFSETHV1]  << 8);

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
				eod = sod + (HEADERSIZEV1 + x - 1) * BITSINABYTE;

				/* Point to the last pulse of the last byte */
				eof = eod + BITSINABYTE - 1;

				/* Trace 'eof' to end of trailer (bit 1 pulses only) */
				h = 0;
				while (eof < tap.len - 1 &&
						h++ < MAXTRAILER &&
						readttbit(eof + 1, lp, sp, tp) == 1)
					eof++;

				if (addblockdef(THISLOADER, sof, sod, eod, eof, xinfo) >= 0)
					i = eof;	/* Search for further files starting from the end of this one */

			} else {
				if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
					i = (-eop);
			}
		}
	}
}

int rackit_describe (int row)
{
	int i, s;
	int hd[HEADERSIZE];
	int en, tp, sp, lp;
	int cb, xor;

	int b, rd_err;

	int variant;


	en = ft[THISLOADER].en;
	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	sprintf(lin, "\n - Pilot : $%02X, Sync : $%02X, Endianness : %s",
		ft[THISLOADER].pv,
		ft[THISLOADER].sv,
		ENDIANNESS_TO_STRING(en));
	strcat(info, lin);

	/* Set read pointer to the beginning of the payload */
	s = blk[row]->p2;

	variant = blk[row]->xi >> 8;

	/* Note: There is some code duplication below, but keep it as it is for clarity */
	if (variant == 0) {
		/* Read header (it's safe to read it here for it was already decoded during the search stage) */
		for (i = 0; i < HEADERSIZE; i++)
			hd[i] = readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);

		xor = hd[XOROFFSET];

		sprintf(lin, "\n - Header Size : %d bytes", HEADERSIZE);
		strcat(info, lin);
		sprintf(lin, "\n - Block Number : $%02X", hd[BLKNUMOFFSET]);
		strcat(info, lin);
		sprintf(lin, "\n - XOR cypher value : $%02X", xor);
		strcat(info, lin);

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

		/* Extract data and test checksum... */
		rd_err = 0;
		cb = blk[row]->xi & 0xFF;

		s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);

		if (blk[row]->dd != NULL)
			free(blk[row]->dd);

		blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

		for (i = 0; i < blk[row]->cx; i++) {
			b = readttbyte(s + (i * BITSINABYTE), lp, sp, tp, en);
			b ^= xor;
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

		/* Display the overall execution address if it's used in a standard way */
		if (blk[row]->cs == 0xFFFC && blk[row]->cx == 2) {
			unsigned int exe_address;

			exe_address = (unsigned int) blk[row]->dd[0] | (((unsigned int) blk[row]->dd[1]) << 8);
			sprintf(lin, "\n - Exe Address : $%04X", exe_address);
			strcat(info, lin);
		}

		blk[row]->cs_exp = cb & 0xFF;
		blk[row]->cs_act = hd[CHKBOFFSET];
		blk[row]->rd_err = rd_err;

		return(rd_err);
	} else {
		/* Read header (it's safe to read it here for it was already decoded during the search stage) */
		for (i = 0; i < HEADERSIZEV1; i++)
			hd[i] = readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);

		sprintf(lin, "\n - Header Size : %d bytes", HEADERSIZEV1);
		strcat(info, lin);
		sprintf(lin, "\n - Block Number : $%02X", hd[BLKNUMOFFSETV1]);
		strcat(info, lin);

		/* Extract load and end locations */
		blk[row]->cs = hd[LOADOFFSETLV1] + (hd[LOADOFFSETHV1] << 8);
		blk[row]->ce = hd[ENDOFFSETLV1]  + (hd[ENDOFFSETHV1]  << 8);

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
		cb = blk[row]->xi & 0xFF;

		s = blk[row]->p2 + (HEADERSIZEV1 * BITSINABYTE);

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

		/* Display the overall execution address if it's used in a standard way */
		if (blk[row]->cs == 0xFFFC && blk[row]->cx == 2) {
			unsigned int exe_address;

			exe_address = (unsigned int) blk[row]->dd[0] | (((unsigned int) blk[row]->dd[1]) << 8);
			sprintf(lin, "\n - Exe Address : $%04X", exe_address);
			strcat(info, lin);
		}

		blk[row]->cs_exp = cb & 0xFF;
		blk[row]->cs_act = hd[CHKBOFFSETV1];
		blk[row]->rd_err = rd_err;

		return(rd_err);
	}
}
