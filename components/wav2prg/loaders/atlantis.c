/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * atlantis.c : format found in some Atlantis tapes (Cavemania...)
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool atlantis_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint16_t exec_address;

  if (functions->get_word_bigendian_func(context, functions, conf, &exec_address) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_bigendian_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_bigendian_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static uint16_t atlantis_thresholds[]={0x180};
static uint8_t atlantis_pilot_sequence[]={0x52,0x42};

static const struct wav2prg_loaders atlantis[] = {
  {
    "Atlantis",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      atlantis_get_block_info,
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
      atlantis_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      2,
      sizeof(atlantis_pilot_sequence),
      atlantis_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(atlantis,1,0,"desc", atlantis);
