/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * alien.c : Convert Alien (Argus Press)
 */

#include "../wav2prg_api.h"

static uint16_t alien_thresholds[]={0x2BC,0x4B0};

static enum wav2prg_sync_result alien_get_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint8_t byte;
  uint32_t i;
  uint8_t pulse;
  enum wav2prg_sync_result res;
  uint32_t min_pilots = 0;
  
  do{
    if (functions->get_pulse_func(context, conf, &pulse) == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
    min_pilots++;
  }while (pulse == 2);

  return (pulse != 0 || min_pilots < conf->min_pilots) ? wav2prg_sync_failure : wav2prg_sync_success;
}

static enum wav2prg_bool alien_get_block_info(struct wav2prg_context *context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info *info)
{
  uint16_t length;
  uint8_t read_byte;
  int i;

  if (functions->get_data_byte_func(context, functions, conf, &read_byte, 0) == wav2prg_false)
    return wav2prg_false;
  if (read_byte != 0)
    return wav2prg_false;
  if (functions->get_data_byte_func(context, functions, conf, &read_byte, 0) == wav2prg_false)
    return wav2prg_false;
  if (read_byte != 3 && read_byte != 7)
    return wav2prg_false;
  for (i = 0; i < 10; i++) {
    if (functions->get_data_byte_func(context, functions, conf, &read_byte, 0) == wav2prg_false)
      return wav2prg_false;
    info->name[i] = read_byte;
  }

  if (functions->get_data_word_func(context, functions, conf, &length) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_data_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  info->end = info->start + length;
  if (functions->get_data_byte_func(context, functions, conf, &read_byte, 0) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_data_byte_func(context, functions, conf, &read_byte, 0) == wav2prg_false)
    return wav2prg_false;
  if (functions->check_checksum_func(context, functions, conf) != wav2prg_checksum_state_correct)
    return wav2prg_false;
  if (functions->get_sync(context, functions, conf, wav2prg_true) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_data_byte_func(context, functions, conf, &read_byte, 0) == wav2prg_false)
    return wav2prg_false;
  if (read_byte != 0xFF)
    return wav2prg_false;
  return wav2prg_true;
}

static const struct wav2prg_loaders alien_functions[] = {
  {
    "Alien",
    {
      NULL,
      NULL,
      alien_get_sync,
      NULL,
      alien_get_block_info,
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
      3,
      alien_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,/*ignored*/
      0x55,
      0,/*ignored*/
      NULL,/*ignored*/
      127,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(alien, 1, 0, "Alien (Argus Press)", alien_functions)
