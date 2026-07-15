/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2010-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * program_block.h : structure storing binary data of a program
 */

#ifndef WAV2PRG_BLOCKS_H
#define WAV2PRG_BLOCKS_H

#include <stdint.h>

struct program_block_info {
  uint16_t start;
  uint16_t end;
  char name[17];
};

struct program_block {
  struct program_block_info info;
  unsigned char data[65536];
};

#endif /* WAV2PRG_BLOCKS_H */
