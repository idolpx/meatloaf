/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * novaload_special.c : Novaload "special" (using Tapclean terminology)
 */

#include "../wav2prg_api.h"

struct novaload_special_private_state {
  enum wav2prg_bool synced_state;
  uint8_t start_of_block;
};

static const struct novaload_special_private_state novaload_special_private_state_model = {
  wav2prg_false
};
static struct wav2prg_generate_private_state novaload_special_generate_private_state = {
  sizeof(novaload_special_private_state_model),
  &novaload_special_private_state_model
};

static uint16_t novaload_special_thresholds[]={0x1f4};
static uint8_t novaload_special_pilot_sequence[]={0xaa,0x55};

static enum wav2prg_sync_result novaload_special_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  struct novaload_special_private_state *state = (struct novaload_special_private_state *)conf->private_state;
  if (state->synced_state)
    return wav2prg_sync_success;
  return functions->get_sync_sequence(context, functions, conf);
}

static enum wav2prg_bool novaload_special_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  struct novaload_special_private_state *state = (struct novaload_special_private_state *)conf->private_state;

  info->end = 0;
  if (!state->synced_state) {
    state->synced_state = wav2prg_true;

    if (functions->get_data_byte_func(context, functions, conf, &state->start_of_block, 0) == wav2prg_false
     || state->start_of_block == 0x00)
      return wav2prg_false;
  }
  else
    functions->reset_checksum_to_func(context, state->start_of_block);
  info->start = state->start_of_block * 256;
  return wav2prg_true;
}

static enum wav2prg_bool novaload_special_get_block_func(struct wav2prg_context*context, const struct wav2prg_functions*functions, struct wav2prg_plugin_conf*conf, struct wav2prg_raw_block*block, uint16_t block_len)
{
  struct novaload_special_private_state *state = (struct novaload_special_private_state *)conf->private_state;
  uint8_t old_start_of_block;
  do 
  {
    old_start_of_block = state->start_of_block;
    if (!functions->get_block_func(context, functions, conf, block, 256))
      return wav2prg_false;
    if (functions->check_checksum_func(context, functions, conf) != wav2prg_checksum_state_correct)
      return wav2prg_false;
    functions->reset_checksum_func(context);
    if (!functions->get_data_byte_func(context, functions, conf, &state->start_of_block, 0))
      return wav2prg_false;
  } while (state->start_of_block == old_start_of_block + 1);
  return wav2prg_true;
}

static enum wav2prg_bool keep_doing_novaload_special(struct wav2prg_observer_context *observer_context,
                                         const struct wav2prg_observer_functions *observer_functions,
                                         const struct program_block *block,
                                         uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct novaload_special_private_state *state = (struct novaload_special_private_state *)conf->private_state;
  return state->start_of_block != 0;
}

static const struct wav2prg_observers novaload_special_observed_loaders[] = {
  {"Novaload Special", {"Novaload Special", NULL, keep_doing_novaload_special}},
  {NULL, {NULL, NULL, NULL}}
};

static const struct wav2prg_loaders novaload_special_functions[] =
{
  {
    "Novaload Special",
    {
      NULL,
      NULL,
      novaload_special_sync,
      NULL,
      novaload_special_get_block_info,
      novaload_special_get_block_func,
      NULL,
      NULL,
      NULL
    },
    {
      lsbf,
      wav2prg_add_checksum,
      wav2prg_compute_checksum_but_do_not_check_it_at_end,
      0,
      2,
      novaload_special_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
      160,
      sizeof(novaload_special_pilot_sequence),
      novaload_special_pilot_sequence,
      1000,
      first_to_last,
      wav2prg_false,
      &novaload_special_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(novaload_special, 1, 0, "Novaload (only special blocks)", novaload_special_functions)
WAV2PRG_OBSERVER(novaload_special, 1,0, novaload_special_observed_loaders)
