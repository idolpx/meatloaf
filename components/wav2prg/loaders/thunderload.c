/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2013
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * thunderload.c : Thunderload, used by some recent games
 */

#include "../wav2prg_api.h"

struct thunderload_private_state {
  uint8_t number_of_microblocks;
  uint8_t current_microblock;
  uint8_t number_of_macroblocks;
};

struct thunderload_private_state thunderload_private_state_model = {
0,0,6
};

static struct wav2prg_generate_private_state thunderload_generate_private_state = {
  sizeof(thunderload_private_state_model),
  &thunderload_private_state_model
};

static uint16_t thunderload_thresholds[]={0x1a0};
static uint8_t thunderload_intro_pilot_sequence[]={0xF0, 0x96};
static uint8_t thunderload_pilot_sequence[]={0xF0, 0x96, 0x00};

static enum wav2prg_bool thunderload_intro_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t ignored;

  if(functions->get_byte_func(context, functions, conf, &ignored) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static void add_char(char* name, char added)
{
  int i;
  for(i=0;i<16;i++){
    if(name[i] == ' '){
      name[i] = added;
      return;
    }
  }
}

static enum wav2prg_bool thunderload_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  struct thunderload_private_state *state =(struct thunderload_private_state *)conf->private_state;

  functions->reset_checksum_to_func(context, 0xaa^conf->sync_sequence[0]^conf->sync_sequence[1]^conf->sync_sequence[2]);
  if (functions->get_data_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if(!functions->get_data_byte_func(context, functions, conf, &state->number_of_microblocks, 0))
    return wav2prg_false;
  if (functions->get_data_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  if(functions->check_checksum_func(context, functions, conf) != wav2prg_checksum_state_correct)
    return wav2prg_false;
  functions->number_to_name_func(conf->sync_sequence[2], info->name);
  functions->number_to_name_func(state->current_microblock + 1, info->name);
  functions->number_to_name_func(state->number_of_microblocks, info->name);
  add_char(info->name, '(');
  add_char(info->name, '/');
  add_char(info->name, ')');
  return wav2prg_true;
}

static enum wav2prg_bool thunderload_get_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t *byte)
{
  uint8_t bit;
  if (!functions->get_bit_func(context, functions, conf, &bit) || bit != 1)
    return wav2prg_false;
  if (!functions->get_bit_func(context, functions, conf, &bit) || bit != 0)
    return wav2prg_false;
  if (!functions->get_bit_func(context, functions, conf, &bit) || bit != 1)
    return wav2prg_false;
  if (!functions->get_bit_func(context, functions, conf, &bit) || bit != 0)
    return wav2prg_false;
  if (!functions->get_bit_func(context, functions, conf, &bit) || bit != 1)
    return wav2prg_false;
  if (!functions->get_bit_func(context, functions, conf, &bit) || bit != 0)
    return wav2prg_false;
  return functions->get_byte_func(context, functions, conf, byte);
};

static const struct wav2prg_loaders thunderload_functions[] ={
  {
    "Thunderload intro",
    {
      NULL,
      NULL,
      NULL,
      thunderload_get_sync,
      thunderload_intro_get_block_info,
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
      thunderload_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_1_bits_followed_by_0,
      0x01,
      sizeof(thunderload_intro_pilot_sequence),
      thunderload_intro_pilot_sequence,
      3,
      first_to_last,
      wav2prg_false,
      &thunderload_generate_private_state
    }
  },
  {
    "Thunderload",
    {
      NULL,
      NULL,
      NULL,
      thunderload_get_sync,
      thunderload_get_block_info,
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
      thunderload_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_1_bits_followed_by_0,
      0x01,
      sizeof(thunderload_pilot_sequence),
      thunderload_pilot_sequence,
      3,
      first_to_last,
      wav2prg_false,
      &thunderload_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(thunderload, 1, 0, "Thunderload", thunderload_functions)

static enum wav2prg_bool recognize_thunderload_first_block_after_intro(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  int i;
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct thunderload_private_state *state =(struct thunderload_private_state *)conf->private_state;

  for (i = 0; i + 13 < block->info.end - block->info.start; i++) {
    if (block->data[i    ] == 0xa5
     && block->data[i + 1] == 0x05
     && block->data[i + 2] == 0xf0
     && block->data[i + 4] == 0xae
     && block->data[i + 5] == block->data[i + 8]
     && block->data[i + 5] == block->data[i + 11]
     && block->data[i + 6] == block->data[i + 9]
     && block->data[i + 6] == block->data[i + 12]
     && block->data[i + 7] == 0xee
     && block->data[i + 10] == 0xad
     && block->data[i + 13] == 0x20){
      int j = i + 16;
      state->number_of_macroblocks = 0;
      while(j + 3 < block->info.end - block->info.start
          && (block->data[j] == 0xe0 || block->data[j] == 0xc9)
          && block->data[j + 1] == state->number_of_macroblocks){
        state->number_of_macroblocks++;
        j = j + 4;
      }
      if (state->number_of_macroblocks > 0){
        i = j;
        break;
      }
    }
  }

  for (; i + 38 < block->info.end - block->info.start; i++) {
    if (block->data[i    ] == 0xa9
     && block->data[i + 2] == 0x8d
     && block->data[i + 3] == 0xfe 
     && block->data[i + 4] == 0xff
     && block->data[i + 5] == 0xa9
     && block->data[i + 7] == 0x8d
     && block->data[i + 8] == 0xff 
     && block->data[i + 9] == 0xff
     && block->data[i + 10] == 0xa9
     && block->data[i + 12] == 0x8d
     && block->data[i + 13] == 0xfa 
     && block->data[i + 14] == 0xff
     && block->data[i + 15] == 0xa9
     && block->data[i + 17] == 0x8d
     && block->data[i + 18] == 0xfb
     && block->data[i + 19] == 0xff
     && block->data[i + 20] == 0xa9
     && block->data[i + 21] == 0x90
     && block->data[i + 22] == 0x8d
     && block->data[i + 23] == 0x0d 
     && block->data[i + 24] == 0xdc
     && block->data[i + 25] == 0xa9
     && block->data[i + 27] == 0x8d
     && block->data[i + 28] == 0x04
     && block->data[i + 29] == 0xdc
     && block->data[i + 30] == 0xa9
     && block->data[i + 32] == 0x8d
     && block->data[i + 33] == 0x05 
     && block->data[i + 34] == 0xdc
     && block->data[i + 35] == 0xad
     && block->data[i + 36] == 0x0d
     && block->data[i + 37] == 0xdc
     && block->data[i + 38] == 0x60
       ) {
      conf->thresholds[0] = (block->data[i + 31] << 8) + block->data[i + 26];
      return wav2prg_true;
    }
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_thunderload(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct thunderload_private_state *state =(struct thunderload_private_state *)conf->private_state;
  if(++state->current_microblock == state->number_of_microblocks){
    state->current_microblock = 0;
    conf->sync_sequence[2]++;
  }
  return conf->sync_sequence[2] < state->number_of_macroblocks;
}

static enum wav2prg_bool recognize_thunderload_intro(struct wav2prg_observer_context *observer_context,
                                           const struct wav2prg_observer_functions *observer_functions,
                                           const struct program_block *block,
                                           uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x2a7
   && block->info.end == 0x316
   && block->data[0x2d4 -0x2a7] == 0xA9
   && block->data[0x2d5 -0x2a7] == 0x51
   && block->data[0x2d6 -0x2a7] == 0x8d
   && block->data[0x2d7 -0x2a7] == 0xfa
   && block->data[0x2d8 -0x2a7] == 0xff
   && block->data[0x2d9 -0x2a7] == 0x8d
   && block->data[0x2da -0x2a7] == 0x04
   && block->data[0x2db -0x2a7] == 0xdd
   && block->data[0x2f4 -0x2a7] == 0xa9
   && block->data[0x2f6 -0x2a7] == 0x8d
   && block->data[0x2f7 -0x2a7] == 0x04
   && block->data[0x2f8 -0x2a7] == 0xdc
   && block->data[0x2f9 -0x2a7] == 0xa9
   && block->data[0x2fb -0x2a7] == 0x8d
   && block->data[0x2fc -0x2a7] == 0x05
   && block->data[0x2fd -0x2a7] == 0xdc
  ){
    conf->thresholds[0] = (block->data[0x2fa - 0x2a7] << 8) + block->data[0x2f5 - 0x2a7];
    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers thunderload_observed_loaders[] = {
  {"Kernal data chunk",{"Thunderload intro", NULL, recognize_thunderload_intro}},
  {"Thunderload intro",{"Thunderload", NULL, recognize_thunderload_first_block_after_intro}},
  {"Thunderload",{"Thunderload", "continuation", recognize_thunderload}},
  {NULL,{NULL,NULL,NULL}}
};

WAV2PRG_OBSERVER(thunderload, 1,0, thunderload_observed_loaders)
