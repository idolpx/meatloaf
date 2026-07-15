/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * turbo220.c : Turbo 220 (sort of a simplified Turbo Tape 64)
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool turbo220_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint16_t entry_point;
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_bigendian_func(context, functions, conf, &entry_point) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static uint16_t turbo220_thresholds[]={263};
static uint8_t turbo220_pilot_sequence[]={9,8,7,6,5,4,3,2,1};

static const struct wav2prg_loaders turbo220_functions[] = {
  {
    "Turbo 220",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      turbo220_get_block_info,
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
      turbo220_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      2,
      sizeof(turbo220_pilot_sequence),
      turbo220_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(turbo220, 1, 0, "Turbo220 (similar to Turbo Tape 64)", turbo220_functions)
