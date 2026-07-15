/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2013
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * dragonload.c : detects Dragonload as variant of Freeload
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool recognize_dragonload(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x800
   && block->info.end == 0xa00){
    int i,j;
    for (i = 0; i + 100 < block->info.end - block->info.start; i++){
      if(block->data[i    ] == 0xa9
      && block->data[i + 5] == 0x8d
      && block->data[i + 6] == 0x04
      && block->data[i + 7] == 0xdc
      && block->data[i + 8] == 0xa9
	  && block->data[i + 9] >= 4
      && block->data[i + 10] == 0x8d
      && block->data[i + 11] == 0x05
      && block->data[i + 12] == 0xdc
      && block->data[i + 89] == 0x4a
      && block->data[i + 90] == 0x4a
      && (block->data[i + 91] == 0x26 || block->data[i + 91] == 0x66)
      && block->data[i + 92] == 0x06
      && block->data[i + 93] == 0xa5
      && block->data[i + 94] == 0x06
      && block->data[i + 95] == 0x90
      && block->data[i + 96] == 0x02
      && block->data[i + 97] == 0xb0
      && block->data[i + 99] == 0xc9){
        conf->thresholds[0] = block->data[i + 1] + (block->data[i + 9] << 8) - 0x400;
        conf->endianness = block->data[i + 91] == 0x26 ? msbf : lsbf;
        conf->pilot_byte = block->data[i + 100];
        for (j = i + 101; j + 6 < block->info.end - block->info.start; j++){
          if(block->data[j    ] == 0x40
          && block->data[j + 1] == 0xc9
          && block->data[j + 2] == conf->pilot_byte
          && block->data[j + 3] == 0xf0
          && block->data[j + 4] == 0xf2
          && block->data[j + 5] == 0xc9){
            conf->sync_sequence[0] = block->data[j + 6];
            conf->checksum_computation = wav2prg_do_not_compute_checksum;
            return wav2prg_true;
          }
        }
      }
    }
  }
  return wav2prg_false;
}

static const struct wav2prg_observers freeload_observers[] = {
  {"Kernal data chunk", {"Freeload", "Dragonload", recognize_dragonload}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(dragonload, 1,0, freeload_observers)
