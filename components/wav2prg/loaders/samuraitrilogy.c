/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * samuraitrilogy.c : Samurai Trilogy
 */

#include "../wav2prg_api.h"

struct samuraitrilogy_private_state {
  enum wav2prg_bool synced_state;
  uint8_t start_of_block;
};

static const struct samuraitrilogy_private_state samuraitrilogy_private_state_model = {
  wav2prg_false
};
static struct wav2prg_generate_private_state samuraitrilogy_generate_private_state = {
  sizeof(samuraitrilogy_private_state_model),
  &samuraitrilogy_private_state_model
};

static uint16_t samuraitrilogy_thresholds[]={0x208};
static uint8_t samuraitrilogy_pilot_sequence[]={0xaa,0x55,0x4A,0x26,0x4A};
static uint8_t samuraitrilogy_data_pilot_sequence[]={0xaa,0x55};

static enum wav2prg_sync_result samuraitrilogy_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  struct samuraitrilogy_private_state *state = (struct samuraitrilogy_private_state *)conf->private_state;
  if (state->synced_state)
    return wav2prg_sync_success;
  return functions->get_sync_sequence(context, functions, conf);
}

static enum wav2prg_bool samuraitrilogy_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  struct samuraitrilogy_private_state *state = (struct samuraitrilogy_private_state *)conf->private_state;

  info->end = 0;
  if (!state->synced_state) {
    state->synced_state = wav2prg_true;

    if (functions->get_byte_func(context, functions, conf, &state->start_of_block) == wav2prg_false
     || state->start_of_block == 0x00)
      return wav2prg_false;
  }
  info->start = state->start_of_block * 256;
  return wav2prg_true;
}

static enum wav2prg_bool samuraitrilogy_data_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t i;
  struct samuraitrilogy_private_state *state = (struct samuraitrilogy_private_state *)conf->private_state;

  if (!state->synced_state) {
    for(i = 0; i < 4; i++)
      if (functions->get_byte_func(context, functions, conf, info->name + i) == wav2prg_false)
        return wav2prg_false;
  }
  return samuraitrilogy_get_block_info(context, functions, conf, info);
}
    
static enum wav2prg_bool samuraitrilogy_get_block_func(struct wav2prg_context*context, const struct wav2prg_functions*functions, struct wav2prg_plugin_conf*conf, struct wav2prg_raw_block*block, uint16_t block_len)
{
  struct samuraitrilogy_private_state *state = (struct samuraitrilogy_private_state *)conf->private_state;
  uint8_t old_start_of_block;
  do 
  {
    old_start_of_block = state->start_of_block;
    if (!functions->get_block_func(context, functions, conf, block, 256))
      return wav2prg_false;
    if (functions->check_checksum_func(context, functions, conf) != wav2prg_checksum_state_correct)
      return wav2prg_false;
    functions->reset_checksum_func(context);
    if (!functions->get_byte_func(context, functions, conf, &state->start_of_block))
      return wav2prg_false;
  } while (state->start_of_block == old_start_of_block + 1);
  return wav2prg_true;
}

static enum wav2prg_bool keep_doing_samuraitrilogy(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct samuraitrilogy_private_state *state = (struct samuraitrilogy_private_state *)conf->private_state;

  return state->start_of_block != 0;
}

static const struct wav2prg_observers samuraitrilogy_observed_loaders[] = {
  {"Samurai Trilogy", {"Samurai Trilogy", NULL, keep_doing_samuraitrilogy}},
  {NULL, {NULL, NULL, NULL}}
};

static enum wav2prg_bool samuraitrilogy_get_loaded_checksum(struct wav2prg_context *context, const struct wav2prg_functions *functions, struct wav2prg_plugin_conf *conf, uint8_t *byte)
{
  return functions->get_byte_func(context, functions, conf, byte);
}

static const struct wav2prg_loaders samuraitrilogy_functions[] =
{
  {
    "Samurai Trilogy",
    {
      NULL,
      NULL,
      samuraitrilogy_sync,
      NULL,
      samuraitrilogy_get_block_info,
      samuraitrilogy_get_block_func,
      NULL,
      samuraitrilogy_get_loaded_checksum,
      NULL
    },
    {
      lsbf,
      wav2prg_add_checksum,
      wav2prg_compute_checksum_but_do_not_check_it_at_end,
      1,
      2,
      samuraitrilogy_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
      160,
      sizeof(samuraitrilogy_pilot_sequence),
      samuraitrilogy_pilot_sequence,
      1000,
      first_to_last,
      wav2prg_false,
      &samuraitrilogy_generate_private_state
    }
  },
  {
    "Samurai Trilogy data",
    {
      NULL,
      NULL,
      samuraitrilogy_sync,
      NULL,
      samuraitrilogy_data_get_block_info,
      samuraitrilogy_get_block_func,
      NULL,
      samuraitrilogy_get_loaded_checksum,
      NULL
    },
    {
      lsbf,
      wav2prg_add_checksum,
      wav2prg_compute_checksum_but_do_not_check_it_at_end,
      1,
      2,
      samuraitrilogy_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
      160,
      sizeof(samuraitrilogy_data_pilot_sequence),
      samuraitrilogy_data_pilot_sequence,
      1000,
      first_to_last,
      wav2prg_false,
      &samuraitrilogy_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(samuraitrilogy, 1, 0, "Samurai Trilogy", samuraitrilogy_functions)
WAV2PRG_OBSERVER(samuraitrilogy, 1,0, samuraitrilogy_observed_loaders)
