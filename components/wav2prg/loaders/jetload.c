/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * jetload.c : Jetload
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool jetload_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
      return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static uint16_t jetload_thresholds[]={0x1e0};

static enum wav2prg_sync_result jetload_get_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint8_t byte;
  uint8_t bit;
  enum wav2prg_sync_result res;

  res = functions->get_sync_sequence(context, functions, conf);
  if (res != wav2prg_sync_success)
    return res;
  if (functions->get_bit_func(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 0)
    return wav2prg_sync_failure;
  if (functions->get_bit_func(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 0)
    return wav2prg_sync_failure;
  if (functions->get_bit_func(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 0)
    return wav2prg_sync_failure;
  if (functions->get_bit_func(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 1)
    return wav2prg_sync_failure;
  if (functions->get_bit_func(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 0)
    return wav2prg_sync_failure;
  if (functions->get_bit_func(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 1)
    return wav2prg_sync_failure;
  if (functions->get_bit_func(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 1)
    return wav2prg_sync_failure;
  if (functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  return byte == 0x2e ? wav2prg_sync_success : wav2prg_sync_failure;
}

static const struct wav2prg_loaders jetload_functions[] = {
  {
    "Jetload",
    {
      NULL,
      NULL,
      jetload_get_sync,
      NULL,
      jetload_get_block_info,
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      lsbf,
      wav2prg_xor_checksum,
      wav2prg_do_not_compute_checksum,
      0,
      2,
      jetload_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
      2,
      0,
      NULL,
      8,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(jetload, 1, 0, "Jetload plug-in", jetload_functions)
