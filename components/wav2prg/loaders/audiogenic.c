/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * audiogenic.c : decodes Audiogenic format and the variant found in Strike Force Cobra
 * and in Special Agent
 */

#include "../wav2prg_api.h"

struct audiogenic_private_state {
  enum
  {
    audiogenic_not_synced,
    audiogenic_synced
  }state;

  uint8_t last_block_loaded;
  uint8_t two_is_an_empty_block;
};

static enum wav2prg_sync_result specialagent_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  struct audiogenic_private_state *state =(struct audiogenic_private_state *)conf->private_state;

  if (state->state != audiogenic_synced){
    uint32_t valid_pulses = 0;
    uint32_t old_valid_pulses;
    uint8_t pulse;

    do{
      old_valid_pulses = valid_pulses;
      if (functions->get_pulse_func(context, conf, &pulse) == wav2prg_false)
        return wav2prg_wrong_pulse_when_syncing;
      valid_pulses = pulse == 2 ? valid_pulses+1 : 0;
    }while(valid_pulses!=0);

    if (old_valid_pulses<5)
      return wav2prg_sync_failure;

    if (functions->get_pulse_func(context, conf, &pulse) == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
    if (pulse != 0 && pulse != 1)
      return wav2prg_sync_failure;
    if (functions->get_pulse_func(context, conf, &pulse) == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
    if (pulse != 0 && pulse != 1)
      return wav2prg_sync_failure;
  }
  return wav2prg_sync_success;
}

static const struct audiogenic_private_state audiogenic_specialagent_private_state_model = {
  audiogenic_not_synced,
  0,
  1,
};
static struct wav2prg_generate_private_state audiogenic_specialagent_generate_private_state = {
  sizeof(audiogenic_specialagent_private_state_model),
  &audiogenic_specialagent_private_state_model
};

static uint16_t audiogenic_thresholds[]={319};
static uint8_t audiogenic_pilot_sequence[]={170};

static uint16_t specialagent_thresholds[]={594,1151};

static enum wav2prg_sync_result audiogenic_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  struct audiogenic_private_state *state =(struct audiogenic_private_state *)conf->private_state;
  if (state->state == audiogenic_not_synced)
    return functions->get_sync_sequence(context, functions, conf);
  return wav2prg_true;
}

static enum wav2prg_bool audiogenic_specialagent_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  struct audiogenic_private_state *state =(struct audiogenic_private_state *)conf->private_state;
  if(state->state != audiogenic_synced && functions->get_byte_func(context, functions, conf, &state->last_block_loaded) == wav2prg_false)
    return wav2prg_false;
  state->state = audiogenic_synced;
  info->start = state->last_block_loaded << 8;
  info->end = 0;
  return wav2prg_true;
}

static enum wav2prg_bool check_if_block_valid(uint8_t new_block, struct audiogenic_private_state *state)
{
  if (new_block == 0 || new_block == 1)
    return wav2prg_false;
  if (new_block == 2 && state->two_is_an_empty_block)
    return wav2prg_false;
  return wav2prg_true;
}

static enum wav2prg_bool audiogenic_specialagent_get_block(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct wav2prg_raw_block* block, uint16_t block_size)
{
  struct audiogenic_private_state *state =(struct audiogenic_private_state *)conf->private_state;
  uint8_t received_blocks = 0;
  uint8_t num_of_blocks = block_size / 256; /* it is always a multiple */
  enum wav2prg_bool new_block_is_consecutive;

  do{
    uint8_t new_block;

    if (!functions->get_block_func(context, functions, conf, block, 256))
      return wav2prg_false;
    if(functions->check_checksum_func(context, functions, conf) != wav2prg_checksum_state_correct)
      return wav2prg_false;
    state->state = audiogenic_not_synced;
    if (++received_blocks == num_of_blocks)
      break;
    if (!check_if_block_valid(state->last_block_loaded, state))
      break;
    if (!functions->get_sync(context, functions, conf, wav2prg_true))
      return wav2prg_false;
    if (!functions->get_byte_func(context, functions, conf, &new_block))
      return wav2prg_false;
    if (!check_if_block_valid(new_block, state)){
      state->last_block_loaded = new_block;
      break;
    }
    state->state = audiogenic_synced;
    new_block_is_consecutive = new_block == state->last_block_loaded + 1;
    state->last_block_loaded = new_block;
  }while(new_block_is_consecutive);
  return wav2prg_true;
}

enum wav2prg_bool recognize_itself(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct audiogenic_private_state *state =(struct audiogenic_private_state *)conf->private_state;

  return state->state == audiogenic_synced
  || (state->last_block_loaded != 0
   && (state->last_block_loaded != 2 || !state->two_is_an_empty_block)
     );
}

static enum wav2prg_bool recognize_hc(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct{
    enum wav2prg_bool register_filled;
    uint8_t value;
  } dd04_7[4] ={{wav2prg_false, 0},{wav2prg_false, 0},{wav2prg_false, 0},{wav2prg_false, 0}};
  const int LDA= 0xA9, LDX=0xA2, STA=0x8D, STX=0x8E;
  uint16_t i = 0x35b - 0x33c;
  enum wav2prg_bool a_initialized = wav2prg_false, x_initialized = wav2prg_false;
  uint8_t a_value, x_value;
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start != 828 || block->info.end != 1020
    || block->data[0] != 3
    || block->data[1] != 0xa7
    || block->data[2] != 2
    || block->data[3] != 0x14
    || block->data[4] != 3
  )
    return wav2prg_false;
  do{
    if (block->data[i] == LDA){
      a_initialized = wav2prg_true;
      a_value = block->data[i + 1];
      i = i + 2;
      continue;
    }
    if (block->data[i] == LDX){
      x_initialized = wav2prg_true;
      x_value = block->data[i + 1];
      i = i + 2;
      continue;
    }
    if (block->data[i] == STA
     && block->data[i + 1] >= 4
     && block->data[i + 1] <= 7
     && block->data[i + 2] == 0xdd
     && a_initialized
    ){
      dd04_7[block->data[i + 1] - 4].register_filled = wav2prg_true;
      dd04_7[block->data[i + 1] - 4].value = a_value;
      i = i + 3;
      continue;
    }
    if (block->data[i] == STX
     && block->data[i + 1] >= 4
     && block->data[i + 1] <= 7
     && block->data[i + 2] == 0xdd
     && x_initialized
    ){
      dd04_7[block->data[i + 1] - 4].register_filled = wav2prg_true;
      dd04_7[block->data[i + 1] - 4].value = x_value;
      i = i + 3;
      continue;
    }
    i = i+1;
  }while(i<=1017-828);
  if(dd04_7[0].register_filled == wav2prg_true
  && dd04_7[1].register_filled == wav2prg_true
  && dd04_7[2].register_filled == wav2prg_true
  && dd04_7[3].register_filled == wav2prg_true
  ){
    conf->thresholds[0] = dd04_7[0].value + (dd04_7[1].value << 8);
    conf->thresholds[1] = dd04_7[2].value + (dd04_7[3].value << 8);
    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers audiogenic_specialagent_observed_loaders[] = {
  {"Audiogenic",{"Audiogenic",  NULL, recognize_itself}},
  {"Special Agent/Strike Force Cobra", {"Special Agent/Strike Force Cobra", NULL, recognize_itself}},
  {"Default C64", {"Special Agent/Strike Force Cobra", NULL, recognize_hc}},
  {NULL,{NULL,NULL,NULL}}
};

static const struct wav2prg_loaders audiogenic[] =
{
  {
    "Audiogenic",
    {
      NULL,
      NULL,
      audiogenic_sync,
      NULL,
      audiogenic_specialagent_get_block_info,
      audiogenic_specialagent_get_block,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_xor_checksum,
      wav2prg_compute_checksum_but_do_not_check_it_at_end,
      0,
      2,
      audiogenic_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      240,
      sizeof(audiogenic_pilot_sequence),
      audiogenic_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      &audiogenic_specialagent_generate_private_state
    }
  },
  {
    "Special Agent/Strike Force Cobra",
    {
      NULL,
      NULL,
      specialagent_sync,
      NULL,/*ignored, overwriting get_sync */
      audiogenic_specialagent_get_block_info,
      audiogenic_specialagent_get_block,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_xor_checksum,
      wav2prg_compute_checksum_but_do_not_check_it_at_end,
      0,
      3,
      specialagent_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      0,                              /*ignored, default get_sync unused*/
      0,
      NULL,
      0,                              /*ignored, default get_sync unused*/
      first_to_last,
      wav2prg_false,
      &audiogenic_specialagent_generate_private_state
    }
  }
  ,{NULL}
};

WAV2PRG_LOADER(audiogenic,1,0,"Audiogenic desc", audiogenic);
WAV2PRG_OBSERVER(audiogenic, 1,0, audiogenic_specialagent_observed_loaders);
