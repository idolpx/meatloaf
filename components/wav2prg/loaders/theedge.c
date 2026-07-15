/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * theedge.c : format found in some The Edge games (Firequest, Almazz, Quo Vadis)
 * Maybe the format name is Booster
 */

#include "../wav2prg_api.h"

static uint16_t theedge_thresholds[]={469};
static int16_t theedge_pulse_length_deviations[]={24, 0};

static enum wav2prg_sync_result theedge_get_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint8_t byte;
  uint32_t i;
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
  if (functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  for(i = 0; i < 9 && byte != 0; i++){
    if (functions->get_bit_func(context, functions, conf, &bit) == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
    byte = (byte << 1) | bit;
  }
  return byte == 0 ? wav2prg_sync_success : wav2prg_sync_failure;
}

static enum wav2prg_bool theedge_get_block_info(struct wav2prg_context *context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info *info)
{
  uint32_t i;
  uint8_t byte;

  for(i = 0; i < 256; i++)
    if(functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
      return wav2prg_false;
  if(functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  return functions->get_sync(context, functions, conf, wav2prg_false);
}

static const struct wav2prg_loaders theedge_functions[] = {
  {
    "The Edge",
    {
      NULL,
      NULL,
      theedge_get_sync,
      NULL,
      theedge_get_block_info,
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_xor_checksum,/*ignored*/
      wav2prg_do_not_compute_checksum,
      0,
      2,
      theedge_thresholds,
      theedge_pulse_length_deviations,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
      0x55,
      0,/*ignored*/
      NULL,/*ignored*/
      1000,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(theedge, 1, 0, "Loader used in some The Edge games, maybe should be called Booster", theedge_functions)
