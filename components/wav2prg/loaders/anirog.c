/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2014
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * anirog.c : Anirog loader (sort of a simplified Turbo Tape 64)
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool anirog_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t first_byte;
  if (functions->get_byte_func(context, functions, conf, &first_byte) == wav2prg_false || first_byte == 0)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static uint16_t anirog_thresholds[]={263};
static uint8_t anirog_pilot_sequence[]={9,8,7,6,5,4,3,2,1};

static const struct wav2prg_loaders anirog_functions[] = {
  {
    "Anirog",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      anirog_get_block_info,
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_xor_checksum,/* ignored, not computing checksum */
      wav2prg_do_not_compute_checksum,
      0,
      2,
      anirog_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      2,
      sizeof(anirog_pilot_sequence),
      anirog_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(anirog, 1, 0, "Anirog loader", anirog_functions)

static enum wav2prg_bool recognize_turbotape_anirog(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x801
   && block->info.end == 0x2100
   && block->data[0x1f6b - 0x801] == 0x26
   && block->data[0x1f6c - 0x801] == 0xBD
   && block->data[0x1f6d - 0x801] == 0xA5
   && block->data[0x1f6e - 0x801] == 0xBD
   && block->data[0x1f6f - 0x801] == 0xC9
   && block->data[0x1f70 - 0x801] == 0x02
   && block->data[0x1f71 - 0x801] == 0xD0
   && block->data[0x1f72 - 0x801] == 0xF5
   && block->data[0x1f73 - 0x801] == 0xA0
   && block->data[0x1f75 - 0x801] == 0x20
   && block->data[0x1f78 - 0x801] == 0xC9
   && block->data[0x1f79 - 0x801] == 0x02
   && block->data[0x1f7a - 0x801] == 0xF0
   && block->data[0x1f7b - 0x801] == 0xF9
   && block->data[0x1f7c - 0x801] == 0xC4
   && block->data[0x1f7d - 0x801] == 0xBD){
        uint8_t new_sync_len = block->data[0x1f74 - 0x801];
        int j, sbyte;
        struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

        observer_functions->change_sync_sequence_length_func(conf, new_sync_len);
        for(j = 0, sbyte = new_sync_len; j < new_sync_len; j++, sbyte--)
          conf->sync_sequence[j] = sbyte;
        
        return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_anirog(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  if (block->info.start == 0x33c
   && block->info.end == 0x3fc
   && block->data[0] == 0x03
   && block->data[3] == 0x34
   && block->data[4] == 0x03
   && block->data[21] == 0x20
   && block->data[22] == 0x9e
   && block->data[23] == 0x03
   && block->data[24] == 0xc9
   && block->data[25] == 0x00
   && block->data[26] == 0xf0
   && block->data[27] == 0xf9
   && block->data[28] == 0x20
   && block->data[29] == 0xd4
   && block->data[30] == 0x03
   && block->data[31] == 0x85
   && block->data[32] == 0xc1
   && block->data[33] == 0x20
   && block->data[34] == 0xd4
   && block->data[35] == 0x03
   && block->data[36] == 0x85
   && block->data[37] == 0xc2
   && block->data[38] == 0x20
   && block->data[39] == 0xd4
   && block->data[40] == 0x03
   && block->data[41] == 0x85
   && block->data[42] == 0x2d
   && block->data[43] == 0x20
   && block->data[44] == 0xd4
   && block->data[45] == 0x03
   && block->data[46] == 0x85
   && block->data[47] == 0x2e){        
    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers anirog_observers[] = {
  {"Anirog", {"Turbo Tape 64", "Anirog", recognize_turbotape_anirog}},
  {"Default C64", {"Anirog", NULL, recognize_anirog}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(anirog, 1,0, anirog_observers)
