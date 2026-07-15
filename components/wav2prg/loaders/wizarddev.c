/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * wizarddev.c : format used by International Karate)
 * (text in loader says "only for use by Wizard Development")
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool wizarddev_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint16_t exec_address;
  uint8_t id;

  if (functions->get_byte_func(context, functions, conf, &id) == wav2prg_false)
    return wav2prg_false;
  functions->number_to_name_func(id, info->name);
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
      return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  info->end++;
  if (functions->get_word_func(context, functions, conf, &exec_address) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static uint16_t wizarddev_thresholds[]={0x180};

static const struct wav2prg_loaders wizarddev_functions[] = {
  {
    "Wizard Development",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      wizarddev_get_block_info,
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
      wizarddev_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_1_bits_followed_by_0,
      2,
      0,
      NULL,
      2000,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(wizarddev, 1, 0, "Wizard Development loader", wizarddev_functions)
