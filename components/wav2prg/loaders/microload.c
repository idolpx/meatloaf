/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * microload.c : Microload tapes
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool microload_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint16_t complement_of_length;

  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &complement_of_length) == wav2prg_false)
    return wav2prg_false;
  info->end = info->start - complement_of_length;
  return wav2prg_true;
}

static uint16_t microload_thresholds[]={0x14d};
static uint8_t microload_pilot_sequence[]={10,9,8,7,6,5,4,3,2,1};

static const struct wav2prg_loaders microload_functions[] = {
  {
    "Microload",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      microload_get_block_info,
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      lsbf,
      wav2prg_xor_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      2,
      microload_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      160,
      sizeof(microload_pilot_sequence),
      microload_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(microload, 1, 0, "Microload loader", microload_functions)
