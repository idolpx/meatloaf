/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2010-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * wavprg_types.h : because C does not have a bool type
 * (well, C99 has, but this whole thing is not written in C99)
 */

#ifndef WAV2PRG_TYPES_H
#define WAV2PRG_TYPES_H

enum wav2prg_bool {
  wav2prg_false,
  wav2prg_true
};

#endif /* WAV2PRG_TYPES_H */
