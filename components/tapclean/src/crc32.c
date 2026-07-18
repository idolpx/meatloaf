/*
 * crc32.c (computes crc32 checksums)
 *
 * Part of project "Final TAP".
 *
 * A Commodore 64 tape remastering and data extraction utility.
 *
 * (C) 2001-2006 Stewart Wilson, Subchrist Software.
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
 *
 * Usage:
 *
 *  unsigned int crc = -1L
 *  crc = crc32(buffer, length, crc)
 *
 */


#include <stdlib.h>

#include "crc32.h"
#include "main.h"	/* For msgout() */

static unsigned int *crc_table;


/*
 * Initialize the CRC calculation table
 */

int crc32_build_crc_table(void)
{
	int i, j;
	unsigned int crc;

	crc_table = (unsigned int*)malloc(256 * sizeof(unsigned int));
	if (crc_table == NULL) {
		msgout("Error: Can't malloc space for CRC table!");
		return -1;
	}

	for (i = 0; i <= 255; i++) {
		crc = i;
		for (j = 8; j > 0; j--) {
			if (crc & 1)
				crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
			else
				crc >>= 1;
			crc_table[i] = crc;
		}
	}

	return 0;
}

/*
 * Return the CRC32 for 'buffer' of length 'count' bytes.
 */

unsigned int crc32_compute_crc(unsigned char *buffer, int count)
{
	unsigned int t1, t2, crc = 0xFFFFFFFF;

	if (count <= 0)
		return 0;

	while (count-- != 0) {
		t1 = (crc >> 8) & 0x00FFFFFF;
		t2 = crc_table[((int)crc ^ *buffer++) & 0xFF];
		crc = t1 ^ t2;
	}

	return crc;
}

/*
 * Free the CRC table...
 */

void crc32_free_crc_table(void)
{
	if (crc_table != NULL)
		free(crc_table);
}

