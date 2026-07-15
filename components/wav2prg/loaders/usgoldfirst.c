/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * usgoldfirst.c : found on many US Gold names. The name precedes start and end addresses
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool usgoldfirst_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t i;

  for(i=0;i<16;i++){
    if (functions->get_byte_func(context, functions, conf, (uint8_t*)info->name + i)  == wav2prg_false)
      return wav2prg_false;
  }
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static uint16_t usgoldfirst_thresholds[]={0x168};
static uint8_t usgoldfirst_pilot_sequence[]={0xFF};

static const struct wav2prg_loaders usgoldfirst_functions[] = {
  {
    "US Gold filename first",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      usgoldfirst_get_block_info,
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_xor_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      2,
      usgoldfirst_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      32,
      sizeof(usgoldfirst_pilot_sequence),
      usgoldfirst_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      NULL
    },
  },
  {NULL}
};

WAV2PRG_LOADER(usgoldfirst, 1, 0, "US Gold loader, filename first", usgoldfirst_functions)
