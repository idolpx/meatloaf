/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2011-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * wav2prg_block_list.c : main processing file to detect programs from pulses
 */
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "wav2prg_block_list.h"

struct block_list_element* new_block_list_element(uint8_t num_pulse_lengths, uint16_t *thresholds){
  struct block_list_element* block = (struct block_list_element*)calloc(1, sizeof(struct block_list_element));

  block->block_status = block_no_sync;
  block->num_pulse_lengths = num_pulse_lengths;
  block->thresholds = malloc(sizeof(uint16_t) * (num_pulse_lengths - 1));
  memcpy(block->thresholds, thresholds, sizeof(uint16_t) * (num_pulse_lengths - 1));

  return block;
}

void free_block_list_element(struct block_list_element* block){
  free(block->thresholds);
  free(block->pulse_length_deviations);
  free(block->syncs);
  free(block->loader_name);
  free(block);
}

