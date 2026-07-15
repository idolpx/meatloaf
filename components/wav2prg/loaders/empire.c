/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * empire.c : format found in some Empire games (Cool Croc Twins, Volfied)
 */

#include "../wav2prg_api.h"

static uint16_t empire_thresholds[]={0x168};
static uint8_t empire_pilot_sequence[]={0x40};

struct empire_private_state {
  enum{
    dont_be_tolerant,
    be_tolerant,
    been_tolerant,
    already_been_tolerant_enough_is_enough
  } tolerance;
};

static const struct empire_private_state empire_private_state_model = {
  dont_be_tolerant
};
static struct wav2prg_generate_private_state empire_generate_private_state = {
  sizeof(empire_private_state_model),
  &empire_private_state_model
};

static enum wav2prg_bool empire_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  struct empire_private_state *state = (struct empire_private_state *)conf->private_state;

  if(functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_word_func(context, functions, conf, &info->end  ) == wav2prg_false)
    return wav2prg_false;
  state->tolerance = be_tolerant;
  return wav2prg_true;
}

static enum wav2prg_bool empire_get_bit(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* bit) {
  struct empire_private_state *state = (struct empire_private_state *)conf->private_state;

  switch(state->tolerance){
  case already_been_tolerant_enough_is_enough:
    return wav2prg_false;
  case been_tolerant:
    state->tolerance = already_been_tolerant_enough_is_enough;
    *bit = 0;
    return wav2prg_true;
  default:
    if(functions->get_bit_func(context, functions, conf, bit) == wav2prg_false){
      if(state->tolerance == dont_be_tolerant)
        return wav2prg_false;
      state->tolerance = been_tolerant;
      *bit = 0;
    }
  }

  return wav2prg_true;
}

static const struct wav2prg_loaders empire_functions[] ={
  {
    "Empire",
    {
      empire_get_bit,
      NULL,
      NULL,
      NULL,
      empire_get_block_info,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_xor_checksum,/*ignored, checksum not computed*/
      wav2prg_do_not_compute_checksum,
      0,
      2,
      empire_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      0x80,
      sizeof(empire_pilot_sequence),
      empire_pilot_sequence,
      127,
      first_to_last,
      wav2prg_false,
      &empire_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(empire, 1, 0, "Empire", empire_functions)

static enum wav2prg_bool recognize_empire(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  if (block->info.start == 0x801
   && block->info.end == 0x8e8
   && block->data[0x81e - 0x801] == 0xa9
   && block->data[0x820 - 0x801] == 0x8d
   && block->data[0x821 - 0x801] == 0x04
   && block->data[0x822 - 0x801] == 0xdc
   && block->data[0x823 - 0x801] == 0xa9
   && block->data[0x825 - 0x801] == 0x8d
   && block->data[0x826 - 0x801] == 0x05
   && block->data[0x827 - 0x801] == 0xdc
   && block->data[0x880 - 0x801] == 0xc9
   && block->data[0x88c - 0x801] == 0xc9
   && block->data[0x881 - 0x801] == block->data[0x88d - 0x801]
   && block->data[0x890 - 0x801] == 0xc9
  ){
    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers freeload_observers[] = {
  {"Kernal data chunk", {"Empire", NULL, recognize_empire}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(empire, 1,0, freeload_observers)
