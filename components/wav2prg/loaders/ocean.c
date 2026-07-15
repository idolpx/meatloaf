/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * ocean.c: old Ocean loader, based on novaloadspecial.c
 */

#include "../wav2prg_api.h"

struct ocean_private_state {
  enum wav2prg_bool synced_state;
  enum wav2prg_bool has_additional_byte_at_start;
  uint8_t eof_byte;
  uint8_t start_of_block;
};

static const struct ocean_private_state ocean_private_state_model = {
  wav2prg_false, wav2prg_false, 0
};
static struct wav2prg_generate_private_state ocean_generate_private_state = {
  sizeof(ocean_private_state_model),
  &ocean_private_state_model
};

static uint16_t ocean_thresholds[]={0x1e0};

static enum wav2prg_sync_result ocean_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  struct ocean_private_state *state = (struct ocean_private_state *)conf->private_state;
  enum wav2prg_sync_result result;
  uint8_t initial_byte;
  
  if (state->synced_state)
    return wav2prg_sync_success;
  result = functions->get_sync_sequence(context, functions, conf);
  if (result != wav2prg_sync_success)
    return result;
  return !state->has_additional_byte_at_start || functions->get_byte_func(context, functions, conf, &initial_byte) ? wav2prg_sync_success : wav2prg_wrong_pulse_when_syncing;
}

static enum wav2prg_bool ocean_get_header(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  struct ocean_private_state *state = (struct ocean_private_state *)conf->private_state;
  uint8_t initial_byte;

  if (functions->get_byte_func(context, functions, conf, &initial_byte) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_byte_func(context, functions, conf, &state->start_of_block) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static enum wav2prg_bool ocean_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  struct ocean_private_state *state = (struct ocean_private_state *)conf->private_state;

  info->end = 0;
  if (!state->synced_state) {
    state->synced_state = wav2prg_true;

    if (ocean_get_header(context, functions, conf) == wav2prg_false)
      return wav2prg_false;
  }
  info->start = state->start_of_block * 256;
  return state->start_of_block != state->eof_byte;
}

static enum wav2prg_bool ocean_get_block_func(struct wav2prg_context*context, const struct wav2prg_functions*functions, struct wav2prg_plugin_conf*conf, struct wav2prg_raw_block*block, uint16_t block_len)
{
  struct ocean_private_state *state = (struct ocean_private_state *)conf->private_state;
  uint8_t old_start_of_block;
  do 
  {
    old_start_of_block = state->start_of_block;
    if (!functions->get_block_func(context, functions, conf, block, 256))
      return wav2prg_false;
    if (!ocean_get_header(context, functions, conf))
      return wav2prg_false;
  } while (state->start_of_block == old_start_of_block + 1);
  return wav2prg_true;
}

static enum wav2prg_bool keep_doing_ocean(struct wav2prg_observer_context *observer_context,
                                         const struct wav2prg_observer_functions *observer_functions,
                                         const struct program_block *block,
                                         uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct ocean_private_state *state = (struct ocean_private_state *)conf->private_state;
  return state->start_of_block != state->eof_byte;
}

static enum wav2prg_bool recognize_ocean(struct wav2prg_observer_context *observer_context,
                                         const struct wav2prg_observer_functions *observer_functions,
                                         const struct program_block *block,
                                         uint16_t start_point){
  uint16_t i;
  
  if (block->info.start != 0x316 || block->info.end < 0x5a0)
    return wav2prg_false;

  for(i = 0; i < block->info.end - block->info.start - 24; i++){
    if (block->data[i   ] == 0x90
     && block->data[i+ 2] == 0xA0
     && block->data[i+ 3] == 0x80
     && block->data[i+ 4] == 0xA5
     && block->data[i+ 5] == 0x02
     && block->data[i+ 6] == 0x84
     && block->data[i+ 7] == 0x02
     && block->data[i+ 8] == 0xA4
     && block->data[i+ 9] == 0x03
     && block->data[i+10] == 0xF0
     && block->data[i+12] == 0x99  
     && block->data[i+13] == 0x0A
     && block->data[i+14] == 0x00
     && block->data[i+15] == 0xC6
     && block->data[i+16] == 0x03
     && block->data[i+17] == 0xD0
     && block->data[i+19] == 0x09
     && block->data[i+20] == 0x00
     && block->data[i+21] == 0xD0
     && block->data[i+23] == 0xE6
     && block->data[i+24] == 0x04){
	  return wav2prg_true;
	}
    if ((block->data[i+ 2] == 0xD0
     && block->data[i+ 3] == 0x09
     && block->data[i+ 4] == 0x78
     && block->data[i+ 5] == 0xA9
     && block->data[i+ 6] == 0x01
     && block->data[i+ 7] == 0x8D
     && block->data[i+20] == 0xA9
     && block->data[i+21] == 0xEA
     && block->data[i+22] == 0x8D)
	 ||(block->data[i+ 2] == 0xD0
     && block->data[i+ 3] == 0x08
     && block->data[i+ 4] == 0x78
     && block->data[i+ 5] == 0xA9
     && block->data[i+ 6] == 0x01
     && block->data[i+ 7] == 0x85
     && block->data[i+19] == 0xA9
     && block->data[i+20] == 0xEA
     && block->data[i+21] == 0x8D)){
      struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
      struct ocean_private_state *state = (struct ocean_private_state *)conf->private_state;
	  state->has_additional_byte_at_start = wav2prg_true;
      if (block->data[i] == 0xC9)
	    state->eof_byte = block->data[i + 1];
      return wav2prg_true;
	}
  }
  return wav2prg_false;
}

static const struct wav2prg_observers ocean_observed_loaders[] = {
  {"Ocean", {"Ocean", NULL, keep_doing_ocean}},
  {"Kernal data chunk", {"Ocean", NULL, recognize_ocean}},
  {NULL, {NULL, NULL, NULL}}
};

static const struct wav2prg_loaders ocean_functions[] =
{
  {
    "Ocean",
    {
      NULL,
      NULL,
      ocean_sync,
      NULL,
      ocean_get_block_info,
      ocean_get_block_func,
      NULL,
      NULL,
      NULL
    },
    {
      lsbf,
      wav2prg_add_checksum,/*ignored*/
      wav2prg_do_not_compute_checksum,
      0,
      2,
      ocean_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
      160,
      0,
      NULL,
      256,
      first_to_last,
      wav2prg_false,
      &ocean_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(ocean, 1, 0, "Ocean old", ocean_functions)
WAV2PRG_OBSERVER(ocean, 1,0, ocean_observed_loaders)
