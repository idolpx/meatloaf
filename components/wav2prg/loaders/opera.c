/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * opera.c : supports Opera Turbo Load
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool recognize_opera_dc(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  if (block->info.start == 0x801
   && block->info.end == 0x9ff) {
    for(;start_point + 19 < block->info.end - block->info.start; start_point++) {
      if(block->data[start_point     ] == 0xA9
      && block->data[start_point +  2] == 0x8D
      && block->data[start_point +  3] == 0xB2
      && block->data[start_point +  4] == 0x09
      && block->data[start_point +  5] == 0xA9
      && block->data[start_point +  7] == 0x8D
      && block->data[start_point +  8] == 0xB3
      && block->data[start_point +  9] == 0x09
      && block->data[start_point + 10] == 0xA9
      && block->data[start_point + 12] == 0x8D
      && block->data[start_point + 13] == 0xB4
      && block->data[start_point + 14] == 0x09
      && block->data[start_point + 15] == 0xA9
      && block->data[start_point + 17] == 0x8D
      && block->data[start_point + 18] == 0xB5
      && block->data[start_point + 19] == 0x09
     ){
        struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
        
        observer_functions->set_info_func(observer_context,
                                          block->data[start_point +  1] + (block->data[start_point +  6] << 8),
                                          block->data[start_point + 11] + (block->data[start_point + 16] << 8),
                                          NULL);
        conf->checksum_type = wav2prg_do_not_compute_checksum;
        conf->thresholds[0] = 692;
        conf->findpilot_type = wav2prg_pilot_tone_made_of_1_bits_followed_by_0;
        conf->min_pilots = 128;
        observer_functions->set_restart_point_func(observer_context, start_point + 20);
        return wav2prg_true;
      }
    }
  }
  return wav2prg_false;
}

static const struct wav2prg_observers opera_observed_loaders[] = {
  {"Kernal data chunk", {"Null loader", "Opera Turbo Load", recognize_opera_dc}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(opera, 1,0, opera_observed_loaders)

