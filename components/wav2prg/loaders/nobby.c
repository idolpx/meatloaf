/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * nobby.c : Nobby The Aardvark
 */

#include "../wav2prg_api.h"

static uint16_t nobby_thresholds[]={0x168};
static uint8_t nobby_sync_sequence[]={0x58};

const struct nobby_private_state {
  uint8_t checksum[3], checked_checksum_byte;
} nobby_private_state_model={{0,0,0},0};
static struct wav2prg_generate_private_state nobby_generate_private_state = {
  sizeof(struct nobby_private_state),
  &nobby_private_state_model
};

static enum wav2prg_sync_result nobby_get_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint8_t byte = 0;
  uint8_t bit;
  uint32_t min_pilots;

  do{
    if(functions->get_bit_func(context, functions, conf, &bit) == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
    byte = (byte << 1) | bit;
  }while(byte != conf->pilot_byte);
  min_pilots = 0;
  do{
    min_pilots++;
    if(functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
  } while (byte == conf->pilot_byte);

  if (min_pilots < conf->min_pilots)
    return wav2prg_sync_failure;

  while (byte != conf->sync_sequence[0]){
    if(functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
  } 
  return wav2prg_sync_success;
}

static enum wav2prg_bool nobby_get_block_info(struct wav2prg_context *context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info *info)
{
  uint32_t i;
  uint8_t byte;
  struct nobby_private_state *state =(struct nobby_private_state *)conf->private_state;

  for(i = 0; i < 16; i++)
    if(functions->get_byte_func(context, functions, conf, info->name + i) == wav2prg_false)
      return wav2prg_false;
  if(functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  for(i = 0; i < 3; i++)
    if(functions->get_byte_func(context, functions, conf, state->checksum + i) == wav2prg_false)
      return wav2prg_false;
  if(functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
    return wav2prg_false;
  /*last byte ignored*/
  return wav2prg_true;
}

static enum wav2prg_bool nobby_get_loaded_checksum(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* byte)
{
  struct nobby_private_state *state =(struct nobby_private_state *)conf->private_state;

  *byte = state->checksum[state->checked_checksum_byte];
  state->checked_checksum_byte = (state->checked_checksum_byte + 1) % 3;
  return wav2prg_true;
}

static const struct wav2prg_loaders nobby_functions[] = {
  {
    "Nobby",
    {
      NULL,
      NULL,
      nobby_get_sync,
      NULL,
      nobby_get_block_info,
      NULL,
      NULL,
      nobby_get_loaded_checksum,
      NULL
    },
    {
      msbf,
      wav2prg_add_checksum,
      wav2prg_compute_and_check_checksum,
      2,
      2,
      nobby_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,/*ignored*/
      0x40,
      1,
      nobby_sync_sequence,
      30,
      first_to_last,
      wav2prg_false,
      &nobby_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(nobby, 1, 0, "Nobby the Aardvark loader", nobby_functions)

