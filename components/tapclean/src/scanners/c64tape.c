/*
 * c64tape.c (C64 ROM Tape)
 *
 * Part of project "Final TAP".
 *
 * A Commodore 64 tape remastering and data extraction utility.
 *
 * (C) 2001-2006 Stewart Wilson, Subchrist Software.
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


#include "../main.h"
#include "../mydefs.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define FIRST 0
#define REPEAT 1

#define PULSESINABYTE 20

/*
 * cbm_readbit...
 *
 * reads two pulses at 'offset' into tap.tmem[] and interprets as...
 * 0=SM (bit 0),  1=MS (bit 1),  2=LM (new data), 3=LS (end of data),
 *
 * returns value of 0-3 on success or -1 on error.
 *
*/

static int cbm_readbit(int pos)
{
	#define SP 0
	#define MP 1
	#define LP 2

	int p1, p2, b1, b2, b1amb = 0, b2amb = 0;

	if (pos < 20 || pos > tap.len - 1)
		return(-1);		/* pos is out of bounds. */

	p1 = tap.tmem[pos];
	p2 = tap.tmem[pos + 1];

	/* resolve pulse 1... */

	if (p1 > ft[CBM_HEAD].sp - tol && p1 < ft[CBM_HEAD].sp + tol) {
		b1 = SP;
		b1amb += 1;
	}
	if (p1 > ft[CBM_HEAD].mp - tol && p1 < ft[CBM_HEAD].mp + tol) {
		b1 = MP;
		b1amb += 2;
	}
	if (p1 > ft[CBM_HEAD].lp - tol && p1 < ft[CBM_HEAD].lp + tol) {
		b1 = LP;
		b1amb += 4;
	}

	/* resolve pulse 2... */

	if (p2 > ft[CBM_HEAD].sp - tol && p2 < ft[CBM_HEAD].sp + tol) {
		b2 = SP;
		b2amb += 1;
	}
	if (p2 > ft[CBM_HEAD].mp - tol && p2 < ft[CBM_HEAD].mp + tol) {
		b2 = MP;
		b2amb += 2;
	}
	if (p2 > ft[CBM_HEAD].lp - tol && p2 < ft[CBM_HEAD].lp + tol) {
		b2 = LP;
		b2amb += 4;
	}

	/* Has b1 ambiguity between S and M pulse? */

	if (b1amb == 3) {
		if ((p1 - ft[CBM_HEAD].sp) < (ft[CBM_HEAD].mp - p1)) {
			b1 = SP;	/* choose closest to ideal. */
			b1amb = 1;
		} else {
			b1 = MP;
			b1amb = 2;
		}
	}

	/* b1 has ambiguity between M and L pulse... */

	if (b1amb == 6) {
		if ((p1 - ft[CBM_HEAD].mp) < (ft[CBM_HEAD].lp - p1)) {
			b1 = MP;	/* choose closest to ideal. */
			b1amb = 2;
		} else {
			b1 = LP;
			b1amb = 4;
		}
	}

	/* b2 has ambiguity between S and M pulse... */

	if (b2amb == 3) {
		if ((p2 - ft[CBM_HEAD].sp) < (ft[CBM_HEAD].mp - p2)) {
			b2 = SP;	/* choose closest to ideal. */
			b2amb = 1;
		} else {
			b2 = MP;
			b2amb = 2;
		}
	}

	/* b2 has ambiguity between M and L pulse... */

	if (b2amb == 6) {
		if ((p2 - ft[CBM_HEAD].mp) < (ft[CBM_HEAD].lp - p2)) {
			b2 = MP;	 /* choose closest to ideal. */
			b2amb = 2;
		} else {
			b2 = LP;
			b2amb = 4;
		}
	}

	/* no ambiguities?... */

	if ((b1amb == 1 || b1amb == 2 || b1amb == 4) && (b2amb == 1 || b2amb == 2 || b2amb == 4)) {
		if (b1 == SP && b2 == MP)	/* SM (0) */
			return(0);
		if (b1 == MP && b2 == SP)	/* MS (1) */
			return(1);
		if (b1 == LP && b2 == MP)	/* LM (new data) */
			return(2);
		if (b1 == LP && b2 == SP)	/* LS (end of data) */
			return(3);
	}

	/* if we reach this point then the signal is unreadable... */

	return -1;
}

/*
 * cbm_readbyte...
 * interprets 20 pulses as a CBM format byte, pos should point at an LM pair (new data).
 * on success: returns resulting byte value 0-255, else -1 on error.
 */

static int cbm_readbyte(int pos)
{
	int i, tcnt = 0, bit, byt = 0;
	char check = 1;		/* start value for checkbit xor is 1 */

	/* check next 20 pulses are not inside a pause and *are* inside the tap... */

	for (i = 0; i < PULSESINABYTE; i++) {
		if (pos + i < PULSESINABYTE || pos + i > tap.len - 1)
			return -1;

		if (is_pause_param(pos + i)) {
			add_read_error(pos);	/* read error, unexpected pause */
			return -1;
		}
	}

	/* verify "New DATA marker" (LM)... */

	if (cbm_readbit(pos) != 2) {
		add_read_error(pos);	/* read error, expected a 'new data' marker. */
		return -1;
	}

	tcnt += 2;		/* skip new-data marker (2 pulses)  */

	/* read (decode) the 8 bits of this byte */

	for (i = 0; i < 8; i++) {
		bit = cbm_readbit(pos + tcnt);
		if (bit == 0)		/* SM (0 bit) */
			byt = byt & (255 - (1 << i));
		if (bit == 1)		/* MS (1 bit) */
			byt = byt | (1 << i);

		if (bit == -1) {	/* pulse did not qualify!... */
			add_read_error(pos + tcnt);
			return -1;
		}

		tcnt += 2;		/* forward to next 2 pulses.. */
		check = check ^ bit;
	}

	bit = cbm_readbit(pos + tcnt);	/* read checkbit */

	if (bit != check) {	/* parity checkbit failed?.. */
		add_read_error(pos + tcnt);	/* read error, cbm checkbit failed */
		return -1;
	}

	return byt;
}

void cbm_search(void)
{
	int i, sof, sod, eod, eof, eof_previous = 0;
	int cnt2, di, len;
	int cbmid, valid, j, is_a_header;
	unsigned char b, pat[32], crcdone = 0;
	int s, e, x;

	if (!quiet)
		msgout("  C64 ROM tape");

	/* clear global header and data buffers... */

	for (i = 0; i < 192; i++)
		cbm_header[i] = 0;
	for (i = 0; i < 65536; i++)
		cbm_program[i] = 0;

	for (i = 20; i < tap.len - PULSESINABYTE; i++) {
		b = cbm_readbyte(i);

		/* find a $09 or an $89... */

		if (b == 0x09 || b == 0x89) {
			for (cnt2 = 0; cnt2 < 9; cnt2++)
				/* decode a 10 byte CBM sequence */
				pat[cnt2] = cbm_readbyte(i + (cnt2 * PULSESINABYTE));

			valid=0;

			if (pat[0] == 0x09 && pat[1] == 0x08 &&
					pat[2] == 0x07 && pat[3] == 0x06 &&
					pat[4] == 0x05 && pat[5] == 0x04 &&
					pat[6] == 0x03 && pat[7] == 0x02 &&
					pat[8] == 0x01) {
				valid = 1;
				cbmid = REPEAT;
				sod = i + (9 * PULSESINABYTE);	/* record first byte of actual data */
			}

			if (pat[0] == 0x89 && pat[1] == 0x88 &&
					pat[2] == 0x87 && pat[3] == 0x86 &&
					pat[4] == 0x85 && pat[5] == 0x84 &&
					pat[6] == 0x83 && pat[7] == 0x82 &&
					pat[8] == 0x81 ) {
				valid = 1;
				cbmid = FIRST;
				sod = i + (9 * PULSESINABYTE);	/* record first byte of actual data */
			}

			/* decode the first byte to discover whether its a header or not... */

			b = cbm_readbyte(sod);

			s = cbm_readbyte(sod + PULSESINABYTE) + 256 * cbm_readbyte (sod + 2*PULSESINABYTE);
			e = cbm_readbyte(sod + 3*PULSESINABYTE) + 256 * cbm_readbyte (sod + 4*PULSESINABYTE);

			/* filetype = 1 - 6, assume its a header. (this COULD fail!) */
			/* Also make a plausibility test by checking load and end address then (luigi) */
			if (b > 0 && b < 6 && e > s)
				is_a_header = 1;
			else
				is_a_header = 0;

			if (valid) {
				sof = i;	/* save the start pos of the first byte of sync-sequence */

				/* trace back to the start of the leader...  */

				while (tap.tmem[sof - 1] > (ft[CBM_HEAD].sp - tol) &&
						tap.tmem[sof - 1] < (ft[CBM_HEAD].sp + tol) &&
						(!is_pause_param(sof - 1)) && (sof - 1) > 19 &&
						(sof - 1) > eof_previous /* Don't step on previous block when there's no gap in between */)
					sof--;

				/* if we traced back to an L pulse we have to adjust... */

				if (!is_pause_param(sof - 1) &&
						tap.tmem[sof - 1] > ft[CBM_HEAD].lp - tol &&
						tap.tmem[sof - 1] < ft[CBM_HEAD].lp + tol)
					sof += 1;

				/* find the last data byte of the block...
				 * (using c16/plus4 taps this method fails, EOF markers are missing,
				 * is it possible C16/Plus4 does not actually use them at all?.)
				 */

				/* LOCATE END OF FILE... */

				/* Expect EOF markers?... */

				if (!noc64eof) {
					/*
					 * The side effect of acknowledging partial Header files is that
					 * the calculated end address/size are wrong
					 */
					for(eod = i; i <= tap.len - 2; i+=2) {
						int bitres = cbm_readbit(i);
						if (bitres == -1)
							break;
						if (bitres == 2)
							eod = i - PULSESINABYTE;
						if (bitres == 3) {
							eod = i - PULSESINABYTE;
							i++;
							break;
						}
					}
					eof = i;	/* overwrite below... */
				}

				/* Not expecting EOF's?...
				 * This just scans through all valid 0,1 and 2 (New data marker) bits
				 * it works well but will just put eod on a next read error if one occurs!..
				 */

				/* just scan through all Bit0,Bit1 & New-Data signals... */

				else {
					do
						b = cbm_readbit(i += 2);
					while ((b == 0 || b == 1 || b == 2) && i < tap.len);

					eod = i - PULSESINABYTE;
					eof = eod + PULSESINABYTE + 1;		/* overwrite below... */
				}

				/* now, we scan through any 'S' pulses (trailer) after the last data marker,
				 * if the first non-S-Pulse is a zero (pause) then we put eof there.
				 * otherwise the following block can have this as its leader.
				 */

				cnt2 = eof;

				/* while we have an S pulse... */

				while (tap.tmem[cnt2] > ft[CBM_HEAD].sp - tol &&
						tap.tmem[cnt2] < ft[CBM_HEAD].sp + tol &&
						cnt2 < tap.len)
					cnt2++;

				/* if it ends with a pause... (allowing for up to 2 pre-pause spikes) */

				if (tap.tmem[cnt2] == 0 || tap.tmem[cnt2 + 1] == 0 ||
						tap.tmem[cnt2 + 2] == 0)
					eof = cnt2 - 1;		/* ...put eof there. */
				else if (cnt2 - eof <= 79)	/* partly fixed: not just a pause but also a spike is ok (luigi) */
					eof = cnt2 - 1;

				/*
				 * location is complete.....
				 * add the block definition to the database and if its the 2nd
				 * data block then try and identify the loader...
				 */

				/* Size of payload (the chunk between sync train and checksum) */
				x = (eod - sod) / PULSESINABYTE;

				if (x > 203 && x != 294)	/* just a precaution to make sure we ID'd the   */
					is_a_header = 0;	/* file correctly. (header must be < 200 bytes) */
								/* see Hover Bovver.tap                         */
								/* add: '500cc GP' headers are 203 bytes.       */
								/* add: 'Ping Pong' headers are 294 bytes.      */

				if (is_a_header) {
					if (addblockdef(CBM_HEAD, sof, sod, eod, eof, cbmid) >= 0)
						eof_previous = eof;

					i = eof;	/* optimize search  */

					/* decode it to 'cbm_header[192]' (only *1st* occurrence) */

					if (cbm_decoded == 0) {
						for (j = 0; j < 192; j++)
							cbm_header[j] = cbm_readbyte(sod + (j * PULSESINABYTE));

						tap.cbmhcrc = crc32_compute_crc(cbm_header, 192);

						cbm_decoded++;
					}
				} else {
					if (addblockdef(CBM_DATA, sof, sod, eod, eof, cbmid))
						eof_previous = eof;

					i = eof;	/* optimize search */

					/*
					 * Here we decode the program block just found and create a CRC for it.
					 * This is used elsewhere to ID the loader for "Fast Scanning" purposes.
					 * We only need to do this for the payload of the 1st CBM data file found.
					 * TODO: we should move this logic outside of the scanner, so we would be
					 *       able to use either the FIRST or REPEAT CBM Data file, based on which
					 *       one is healthy (verified checksum and data size matches what is
					 *       set in the header).
					 */

					if (cbmid == REPEAT && !crcdone) {

						/* decode block... (and generate CRC) */

						for (di = sod, cnt2 = 0; di < eod; di += PULSESINABYTE)
							cbm_program[cnt2++] = cbm_readbyte(di);
						len = (eod - sod) / PULSESINABYTE;

						tap.cbmcrc = crc32_compute_crc(cbm_program, len);	/* store program crc globally */
						tap.cbmdatalen = len;

						crcdone = 1;
					}
				}
			}
		}
	}
}

int cbm_describe(int row)
{
	int s, i, j, b, rd_err, cb, startadr;
	char tmp[256];
	unsigned char hd[32];
	char fn[256];
	char str[2000];

	/* data file start,end. these are set by a 'header' describe
	* and used by any subsequent 'data' describe.
	*/

	static int _dfs = 0, _dfe = 0, _dfx = 0;

	startadr = C64_BASIC_START_ADDR;
	if (c20 == TRUE || c16 == TRUE)
		startadr = VIC20C16_BASIC_START_ADDR;

	// SEQ FILE support

	// This method is not complete and it is unreliable if there are
	// unrecognized blocks, so that do NOT use it.
#ifdef CHAINEDCBMSEQSUPPORT
	// Keep a local variable to chain together SEQ files
	static int _dseqindex = 0;
#endif

	// Read the block type (can't fail if the search stage was successful)
	if (blk[row]->lt == CBM_HEAD) {
		// Get header info offset
		s = blk[row]->p2;

		// Get type
		hd[0] = cbm_readbyte(s);

		// Type is SEQ Data
		if (hd[0] == 0x02) {
#ifdef CHAINEDCBMSEQSUPPORT
			// Is it first SEQ?
			if (_dseqindex == 0) {
#endif
				// Use the info inside CBM block
				// Note: force it in case CBM Header is broken
				_dfs = 0x033C;
				_dfe = 0x03FC;
				_dfx = 0x00C0;
#ifdef CHAINEDCBMSEQSUPPORT
			}
#endif

			// Force block infos into blk struct
			blk[row]->cs = _dfs;
			blk[row]->ce = _dfe - 1;
			blk[row]->cx = _dfe - _dfs;

#ifdef CHAINEDCBMSEQSUPPORT
			// Update static info for next block in a chain.
			// Anyway the files won't be chained because of the
			// REPEAT copies!
			if (blk[row]->xi == REPEAT) {
				_dfs += 0x00C0;
				_dfe += 0x00C0;
			}
#endif

			// Fill in the info buffer
			strcat(info, "\n - File ID : ");
			sprintf(lin, blk[row]->xi == REPEAT?"REPEAT":"FIRST");
			strcat(info, lin);

			strcat(info, "\n - DATA FILE type : Data block for SEQ file");

		}
#ifdef CHAINEDCBMSEQSUPPORT
		else
			_dseqindex = 0;
#endif
	}
	// END - SEQ FILE support


	if (blk[row]->lt == CBM_HEAD && hd[0] != 0x02) {

		/* decode the first 21 bytes... */

		s = blk[row]->p2;
		for (i = 0; i < 21; i++)
			hd[i] = cbm_readbyte(s + (i * PULSESINABYTE));

		if (blk[row]->xi == REPEAT) {
			sprintf(lin, "\n - File ID : REPEAT");
			strcat(info, lin);
		}

		if (blk[row]->xi == FIRST) {
			sprintf(lin, "\n - File ID : FIRST");
			strcat(info, lin);
		}

		/* get info about this header... */

		blk[row]->cs= 0x033C;
		blk[row]->cx= ((blk[row]->p3 - blk[row]->p2) /PULSESINABYTE);
		blk[row]->ce= blk[row]->cs + blk[row]->cx -1;

		/* now extract info about the related DATA block... */

		b = hd[0];

		if (b == 1)
			strcpy(tmp, " - DATA FILE type : BASIC");
//		if (b == 2)
//			strcpy(tmp, " - DATA FILE type : Data block for SEQ file");
		if (b == 3)
			strcpy(tmp, " - DATA FILE type : PRG");
		if (b == 4)
			strcpy(tmp, " - DATA FILE type : SEQ");
		if (b == 5)
			strcpy(tmp, " - DATA FILE type : End-of-Tape");

		sprintf(lin, "\n%s", tmp);
		strcat(info, lin);

		_dfs = hd[1] + (hd[2] << 8);	/* remember load address of DATA block. */
		_dfe = hd[3] + (hd[4] << 8);	/* remember end address of DATA block.  */
		_dfx = _dfe - _dfs;		/* remember size of DATA block.         */

		sprintf(lin, "\n - DATA FILE Load address : $%04X", _dfs);
		strcat(info, lin);
		if (hd[0] == 1 && _dfs != startadr) {
			sprintf(lin, " (fake address, actual=$%04X)", startadr);
			strcat(info, lin);
		}

		sprintf(lin, "\n - DATA FILE End address : $%04X", _dfe);
		strcat(info, lin);
		if (hd[0] == 1 && _dfs != startadr) {
			sprintf(lin, " (fake address, actual=$%04X)", startadr + _dfx);
			strcat(info, lin);
		}

		sprintf(lin, "\n - DATA FILE Size (calculated) : %d bytes", _dfx);
		strcat(info, lin);

		/* BASIC filetypes always load to $0801 (C64)/$1001 (VIC20)*/

		if (hd[0] == 1 && s != startadr) {
			_dfs = startadr;
			_dfe = _dfs + _dfx;
		}

		/* extract file name... */

		for (i = 0, j = 0; i < 16; i++) {
//			if (hd[i + 5] < 128)	/* the safe conversion is done in pet2text() later here (luigi) */
			fn[j++] = hd[i + 5];
		}
		fn[j] = 0;

		trim_string(fn);
		pet2text(str, fn);

		if (blk[row]->fn != NULL)
			free(blk[row]->fn);
		blk[row]->fn = (char*)malloc(strlen(str) + 1);
		strcpy(blk[row]->fn, str);

		if (strcmp(tap.cbmname, "") == 0)	/* record the 1st found CBM name in 'tap.cbmname' */
			strcpy(tap.cbmname, str);
	}

	if (blk[row]->lt == CBM_DATA) {
		if (blk[row]->xi == REPEAT) {
			sprintf(lin, "\n - File ID : REPEAT");
			strcat(info, lin);
		}
		if (blk[row]->xi == FIRST) {
			sprintf(lin, "\n - File ID : FIRST");
			strcat(info, lin);
		}

		blk[row]->cs = _dfs;
		blk[row]->ce = _dfe - 1;
		blk[row]->cx = ((blk[row]->p3 - blk[row]->p2) / PULSESINABYTE);

		/* report inconsistancy between size in header and actual size... */

		if (blk[row]->cx != (_dfe - _dfs)) {
			sprintf(lin, " (Warning, Data size differs from header info!.)");
			strcat(info, lin);
		}
	}

	/* common code for both headers and data files... */

	/* extract data and test checksum... */

	rd_err = 0;
	cb = 0;
	s = (blk[row]->p2);

	if (blk[row]->dd != NULL)
		free(blk[row]->dd);
	blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

	for (i = 0; i < blk[row]->cx; i++) {
		b = cbm_readbyte(s + (i * PULSESINABYTE));
		if (b == -1)
			rd_err++;
		else {
			cb ^= b;
			blk[row]->dd[i] = b;
		}
	}
	b = cbm_readbyte(s + (i * PULSESINABYTE));	/* read the actual checkbyte. */
	blk[row]->cs_exp = cb & 0xFF;
	blk[row]->cs_act = b;
	blk[row]->rd_err = rd_err;

	/* get pilot & trailer lengths */

	blk[row]->pilot_len = blk[row]->p2 - blk[row]->p1 - (9 * PULSESINABYTE);
	blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 - PULSESINABYTE - 1;

	return 0;
}

