/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2010-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * wav2prg_block_list.h : main processing file to detect programs from pulses
 */
#ifndef WAV2PRG_BLOCK_LIST_H
#define WAV2PRG_BLOCK_LIST_H

#include "program_block.h"
#include "checksum_state.h"
#include "wavprg_types.h"

struct block_syncs {
  uint32_t start_sync;
  uint32_t end_sync;
  uint32_t end;
};

struct wav2prg_plugin_conf;

struct block_list_element {
  struct program_block block;
  enum {
    block_no_sync,
    block_sync_no_info,
    block_sync_invalid_info,
    block_error_before_end,
    block_checksum_expected_but_missing,
    block_complete
  } block_status;
  uint16_t real_start;
  uint16_t real_end;
  char* loader_name;
  enum wav2prg_checksum_state state;
  uint8_t num_pulse_lengths;
  uint16_t *thresholds;
  int16_t *pulse_length_deviations;
  uint32_t num_of_syncs;
  struct block_syncs* syncs;
  uint32_t end_of_info;
  uint32_t last_valid_data_byte;
  enum wav2prg_bool opposite_waveform;
  struct block_list_element* next;
};

#ifdef __cplusplus
extern "C" {
#endif

struct block_list_element* new_block_list_element(uint8_t num_pulse_lengths, uint16_t *thresholds);
void free_block_list_element(struct block_list_element* block);

#ifdef __cplusplus
}
#endif


#endif /* WAV2PRG_BLOCK_LIST_H */
