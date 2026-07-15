/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2010-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * wav2prg_display_interface.h : functions displaying diagnostic output while converting from tape
 */
#ifndef WAV2PRG_DISPLAY_INTERFACE_H
#define WAV2PRG_DISPLAY_INTERFACE_H


#include "wav2prg_api.h"

struct display_interface_internal;
struct program_block_info;
struct plugin_tree;
struct block_syncs;

#include "checksum_state.h"

struct wav2prg_observed_loaders;

struct wav2prg_display_interface {
  void(*try_sync)(struct display_interface_internal*, const char* loader_name, const char* observation);
  void(*sync)(struct display_interface_internal*, uint32_t info_pos, struct program_block_info*);
  void(*progress)(struct display_interface_internal*, uint32_t pos);
  void(*block_progress)(struct display_interface_internal*, uint16_t pos);
  void(*checksum)(struct display_interface_internal*, enum wav2prg_checksum_state, uint32_t checksum_start, uint32_t checksum_end, uint8_t expected, uint8_t computed);
  void(*end)(struct display_interface_internal*, unsigned char valid, enum wav2prg_checksum_state, char loader_has_checksum, uint32_t num_syncs, struct block_syncs *syncs, uint32_t last_valid_pos, uint16_t bytes, enum wav2prg_block_filling filling);
};

#endif /* WAV2PRG_DISPLAY_INTERFACE_H */
