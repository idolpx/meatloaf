/*
 * burner_var.c (by Luigi Di Fraia, Aug 2006)
 * Based on burner.c
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
 * Single on tape: No
 * Sync: Byte
 * Header: Yes
 * Data: Continuous
 * Checksum: No
 * Post-data: Yes
 * Trailer: Yes
 * Trailer homogeneous: No
 */

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define THISLOADER	BURNERVAR

#define BITSINABYTE	8	/* a byte is made up of 8 bits here */

#define SYNCSEQSIZE	1	/* amount of sync bytes */
#define MAXTRAILER	16	/* max amount of trailer pulses read in */

#define HEADERSIZE	4	/* size of block header */

#define LOADOFFSETH	1	/* load location (MSB) offset inside header */
#define LOADOFFSETL	0	/* load location (LSB) offset inside header */
#define ENDOFFSETH	3	/* end  location (MSB) offset inside header */
#define ENDOFFSETL	2	/* end  location (LSB) offset inside header */

#define POSTDATASIZE	3	/* size in bytes of the MANDATORY information
				   that is found after file data */

#define CBMXORDECRYPT	0x59	/* use this value to decrypt CBM header bytes */

#define V1PILOTOFFSET	0x87	/* offset to p.v. inside CBM header */
#define V1SYNCOFFSET	0x92	/* offset to s.v. inside CBM header */
#define V1ENDIANOFFSET	0x82	/* offset to ROR/ROL OPC inside CBM header */

#define V2PILOTOFFSET	0x89	/* offset to p.v. inside CBM header */
#define V2SYNCOFFSET	0x94	/* offset to s.v. inside CBM header */
#define V2ENDIANOFFSET	0x84	/* offset to ROR/ROL OPC inside CBM header */

#define LEGPILOTOFFSET	0x88	/* offset to p.v. inside CBM header */
#define LEGSYNCOFFSET	0x93	/* offset to s.v. inside CBM header */
#define LEGENDIANOFFSET	0x83	/* offset to ROR/ROL OPC inside CBM header */

//#define ENABLE_LEGACY_BURNER_SUPPORT

enum {
	BURNERVAR_NOMATCH=-1,
	BURNERVAR_NOTFOUND=0,
	/* Keep the above order */
	BURNERVAR_FIRST,
	BURNERVAR_SECOND,
#ifdef ENABLE_LEGACY_BURNER_SUPPORT
	BURNERVAR_LEGACY
#endif
};

/*
 * Check for genuine/known variant
 *
 * Returns:
 *  - BURNERVAR_NOTFOUND if CBM Data block was not found at index cbm_index
 *  - BURNERVAR_NOMATCH  if CBM data does not contain a known variant
 *  - <variant number> otherwise (see enum definition)
 */
static int burnervar_find_variant (int cbm_index, int *pv, int *sv, int *en)
{
	int variant = BURNERVAR_NOTFOUND;

	int ib;			/* condition variable */

	ib = find_decode_block(CBM_HEAD, cbm_index);
	if (ib == -1)
		return variant;	/* failed to locate cbm header. */

	variant = BURNERVAR_NOMATCH;

	/* Basic validation before accessing array elements */
	if (blk[ib]->cx < V1SYNCOFFSET + 1)
		return variant;

	*pv = blk[ib]->dd[V1PILOTOFFSET]  ^ CBMXORDECRYPT;
	*sv = blk[ib]->dd[V1SYNCOFFSET]   ^ CBMXORDECRYPT;
	*en = blk[ib]->dd[V1ENDIANOFFSET] ^ CBMXORDECRYPT;

	/* MSbF => ROL ($26) or.. LSbF => ROR ($66)  */
	if (*en == OPC_ROL || *en == OPC_ROR)
		variant = BURNERVAR_FIRST;

#ifdef ENABLE_LEGACY_BURNER_SUPPORT
	/* Legacy Burner support */
	if (variant == BURNERVAR_NOMATCH) {
		/* Basic validation before accessing array elements */
		if (blk[ib]->cx < LEGSYNCOFFSET + 1)
			return variant;

		*pv = blk[ib]->dd[LEGPILOTOFFSET] ^ CBMXORDECRYPT;
		*sv = blk[ib]->dd[LEGSYNCOFFSET] ^ CBMXORDECRYPT;
		*en = blk[ib]->dd[LEGENDIANOFFSET] ^ CBMXORDECRYPT;

		/* MSbF => ROL ($26) or.. LSbF => ROR ($66)  */
		if(*en == OPC_ROL || *en == OPC_ROR)
			variant = BURNERVAR_LEGACY;
	}
#endif

	if (variant == BURNERVAR_NOMATCH) {
		/* Basic validation before accessing array elements */
		if (blk[ib]->cx < V2SYNCOFFSET + 1)
			return variant;

		*pv = blk[ib]->dd[V2PILOTOFFSET] ^ CBMXORDECRYPT;
		*sv = blk[ib]->dd[V2SYNCOFFSET] ^ CBMXORDECRYPT;
		*en = blk[ib]->dd[V2ENDIANOFFSET] ^ CBMXORDECRYPT;

		/* MSbF => ROL ($26) or.. LSbF => ROR ($66)  */
		if(*en == OPC_ROL || *en == OPC_ROR)
			variant = BURNERVAR_SECOND;
	}

	return variant;
}

void burnervar_search (void)
{
	int i, h;			/* counters */
	int sof, sod, eod, eof, eop;	/* file offsets */
	int hd[HEADERSIZE];		/* buffer to store block header info */

	int pv, sv;			/* dynamically discovered pilot & sync byte */
	int en, tp, sp, lp;		/* encoding parameters */

	unsigned int s, e;		/* block locations referred to C64 memory */
	unsigned int x; 		/* block size */

	int cbm_index;			/* Index of the CBM data block to get info from */

	int variant;


	tp = ft[THISLOADER].tp;
	sp = ft[THISLOADER].sp;
	lp = ft[THISLOADER].lp;

	if (!quiet)
#ifdef ENABLE_LEGACY_BURNER_SUPPORT
		msgout("  Burner (+Mastertronic Variants)");
#else
		msgout("  Burner (Mastertronic Variants)");
#endif

	/*
	 * First we retrieve the burner variables from the CBM header.
	 * We use CBM HEAD index # 1 as we assume the tape image contains
	 * a single game.
	 * For compilations we should search and find the relevant file
	 * using the search code found e.g. in Biturbo.
	 */
	cbm_index = 1;

	variant = burnervar_find_variant (cbm_index, &pv, &sv, &en);
	if (variant <= BURNERVAR_NOTFOUND)
		return;

	/* Convert en from the OPCode to any of the internally used values */
	en = (en == OPC_ROL) ? MSbF : LSbF;

	/*
	 * We now need to feed back the discovered info into the ft table
	 * in order for find_pilot() to use the discovered values
	 */
	ft[THISLOADER].pv = pv;
	ft[THISLOADER].sv = sv;	/* is this really needed by find_pilot()? */
	ft[THISLOADER].en = en;

	if (!quiet) {
#ifdef ENABLE_LEGACY_BURNER_SUPPORT
		sprintf(lin, "  Burner format found: pv=$%02X, sv=$%02X, en=%s",
			pv,
			sv,
			ENDIANNESS_TO_STRING(en));
#else
		sprintf(lin, "  Burner variant found: pv=$%02X, sv=$%02X, en=%s",
			pv,
			sv,
			ENDIANNESS_TO_STRING(en));
#endif
		msgout(lin);
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

			/* Point to the first pulse of the exe ptr, MSB */
			/* Note: + POSTDATASIZE because there's a EOF marker and an exe ptr in v1 and v2 */
			if (variant == BURNERVAR_FIRST || variant == BURNERVAR_SECOND)
				eod = sod + (HEADERSIZE + x + POSTDATASIZE - 1) * BITSINABYTE;
#ifdef ENABLE_LEGACY_BURNER_SUPPORT
			/* Legacy Burner support */
			/* Point to the first pulse of the last data byte (that's final) */
			else
				eod = sod + (HEADERSIZE + x - 1) * BITSINABYTE;
#endif

			/* Point to the last pulse of the exe ptr MSB/last data byte for Burner legacy */
			eof = eod + BITSINABYTE - 1;

			/* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses) */
			h = 0;
			while (eof < tap.len - 1 &&
					h++ < MAXTRAILER &&
					readttbit(eof + 1, lp, sp, tp) >= 0)
				eof++;

			if (addblockdef(THISLOADER, sof, sod, eod, eof, variant) >= 0)
				i = eof;	/* Search for further files starting from the end of this one */

		} else {
			if (eop < 0)	/* find_pilot failed (too few/many), set i to failure point. */
				i = (-eop);
		}
	}
}

int burnervar_describe (int row)
{
	int i, s;
	int hd[HEADERSIZE];
	int pd[POSTDATASIZE];

	int en, tp, sp, lp;

	int b, rd_err;


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

	/* Read header (it's safe to read it here for it was already decoded during the search stage) */
	for (i = 0; i < HEADERSIZE; i++)
		hd[i] = readttbyte(s + i * BITSINABYTE, lp, sp, tp, en);

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
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - (BITSINABYTE - 1);

	/* if there IS pilot then disclude the sync byte */
	if (blk[row]->pilot_len > 0)
		blk[row]->pilot_len -= SYNCSEQSIZE;

	/* Extract data */
	rd_err = 0;

	s = blk[row]->p2 + (HEADERSIZE * BITSINABYTE);

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

#ifdef ENABLE_LEGACY_BURNER_SUPPORT
	if (blk[row]->xi != BURNERVAR_LEGACY) {
#endif
		/* Read post-data */
		for (i = 0; i < POSTDATASIZE; i++) {
			b = readttbyte(s + (blk[row]->cx + i) * BITSINABYTE, lp, sp, tp, en);

			/* Increase read errors for this data is mandatory for the correct execution of the software */
			if (b != -1) {
				pd[i] = b;
			} else {
				rd_err++;

				/* for experts only */
				sprintf(lin, "\n - Read Error on post-data byte @$%X (offset: $%02X)", s + (blk[row]->cx + i) * BITSINABYTE, i);
				strcat(info, lin);

				break;
			}
		}

		/* Print EOF marker only if it was read in properly */
		strcat(info, "\n - Marker : ");

		if (i > 0)
			strcat(info, b == 0 ? "not EOF" : "EOF");
		else
			strcat(info, "---");

		/* Print execution ptr only if it was read in properly */
		strcat(info, "\n - Execution Ptr : ");

		if (i == POSTDATASIZE) {
			sprintf(lin, "$%04X", (pd[2]<<8) + pd[1]);
			strcat(info, lin);
		} else {
			strcat(info, "$----");
		}

#ifdef ENABLE_LEGACY_BURNER_SUPPORT
	}
#endif

	blk[row]->rd_err = rd_err;

	return(rd_err);
}
