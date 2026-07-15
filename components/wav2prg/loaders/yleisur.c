/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * yleisur.c : A plug-in to convert the program in thread http://www.lemon64.com/forum/viewtopic.php?t=36606
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool recognize_turbotape_yleisur(struct wav2prg_observer_context *observer_context,
                                                     const struct wav2prg_observer_functions *observer_functions,
                                                     const struct program_block *block,
                                                     uint16_t start_point)
{
  if (block->info.start == 0x801
   && block->info.end   == 0x1823
   && block->data[0x175d - 0x801] == 0xA0
   && block->data[0x175f - 0x801] == 0x20
   && block->data[0x1760 - 0x801] == 0x02
   && block->data[0x1761 - 0x801] == 0x9F
   && block->data[0x1762 - 0x801] == 0xC9
   && block->data[0x1763 - 0x801] == block->data[0x175a - 0x801]
   && block->data[0x1764 - 0x801] == 0xF0
   && block->data[0x1765 - 0x801] == 0xF9
 ){
    uint8_t new_sync_len = block->data[0x175e - 0x801];
    int j, sbyte;
    struct wav2prg_plugin_conf* conf = observer_functions->get_conf_func(observer_context);

    observer_functions->change_sync_sequence_length_func(conf, new_sync_len);
    for(j = 0, sbyte = new_sync_len; j < new_sync_len; j++, sbyte--)
      conf->sync_sequence[j] = sbyte;
    conf->pilot_byte = block->data[0x1763 - 0x801];
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_yleisur(struct wav2prg_observer_context *observer_context,
                                           const struct wav2prg_observer_functions *observer_functions,
                                           const struct program_block *block,
                                           uint16_t start_point){
  static uint16_t yleisur_thresholds[]={263};
  static uint8_t yleisur_pilot_sequence[]={16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
  const struct wav2prg_plugin_conf yleisur_conf =
  {
      msbf,
      wav2prg_xor_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      2,
      yleisur_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      0x80,
      sizeof(yleisur_pilot_sequence),
      yleisur_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      NULL
  };

  if (block->info.start == 828
   && block->info.end == 1020
   && (block->data[0x3c5 - 0x33c] ^ 0xc5) == 0xa2
   && (block->data[0x3c7 - 0x33c] ^ 0xc7) == 0xa0
   && (block->data[0x3c9 - 0x33c] ^ 0xc9) == 0x86
   && (block->data[0x3ca - 0x33c] ^ 0xca) == 0xac
   && (block->data[0x3cb - 0x33c] ^ 0xcb) == 0x84
   && (block->data[0x3cc - 0x33c] ^ 0xcc) == 0xad
   && (block->data[0x3cd - 0x33c] ^ 0xcd) == 0xa2
   && (block->data[0x3cf - 0x33c] ^ 0xcf) == 0xa0
   && (block->data[0x3d1 - 0x33c] ^ 0xd1) == 0x86
   && (block->data[0x3d2 - 0x33c] ^ 0xd2) == 0xae
   && (block->data[0x3d3 - 0x33c] ^ 0xd3) == 0x84
   && (block->data[0x3d4 - 0x33c] ^ 0xd4) == 0xaf
     ){
    observer_functions->set_info_func(observer_context,
     (block->data[0x3c6 - 0x33c] ^ 0xc6) + ((block->data[0x3c8 - 0x33c] ^ 0xc8)<<8),
     (block->data[0x3ce - 0x33c] ^ 0xce) + ((block->data[0x3d0 - 0x33c] ^ 0xd0)<<8),
      NULL);
    observer_functions->use_different_conf_func(observer_context, &yleisur_conf);

    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers yleisur_one_loader[] = {
  {"Default C64", {"Null loader", "Yleisur", recognize_yleisur}},
  {"Null loader", {"Turbo Tape 64", "Yleisur", recognize_turbotape_yleisur}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(yleisur, 1,0, yleisur_one_loader)
