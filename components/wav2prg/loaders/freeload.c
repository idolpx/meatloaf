/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * freeload.c : Freeload and many variants of it
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool freeload_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static uint16_t freeload_thresholds[]={0x168};
static uint8_t freeload_pilot_sequence[]={90};

static const struct wav2prg_loaders freeload_functions[] = {
  {
    "Freeload",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      freeload_get_block_info,
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
      freeload_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      64,
      sizeof(freeload_pilot_sequence),
      freeload_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      NULL
    },
  },
  {NULL}
};

WAV2PRG_LOADER(freeload, 1, 0, "Freeload plug-in", freeload_functions)
