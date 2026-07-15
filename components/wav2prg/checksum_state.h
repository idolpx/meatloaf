/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2010-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * checksum_state.h : has the file loaded correctly?
 */

#ifndef CHECKSUM_STATE_H
#define CHECKSUM_STATE_H

enum wav2prg_checksum_state {
  wav2prg_checksum_state_unverified,
  wav2prg_checksum_state_correct,
  wav2prg_checksum_state_load_error
};

#endif /* CHECKSUM_STATE_H */

