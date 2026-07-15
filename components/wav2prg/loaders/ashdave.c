/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * ashdave.c : not sure all games using this are by Ash & Dave...
 */

#include "../wav2prg_api.h"

static uint16_t ashdave_thresholds[]={0x1e0};
static uint8_t ashdave_pilot_sequence[]={0x40};

struct ashdave_private_state {
  enum{
    dont_be_tolerant,
    be_tolerant,
    already_been_tolerant_enough_is_enough
  } tolerance;
};

static const struct ashdave_private_state ashdave_private_state_model = {
  dont_be_tolerant
};
static struct wav2prg_generate_private_state ashdave_generate_private_state = {
  sizeof(ashdave_private_state_model),
  &ashdave_private_state_model
};

static enum wav2prg_bool ashdave_get_bit(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* bit) {
  struct ashdave_private_state *state = (struct ashdave_private_state *)conf->private_state;

  switch(state->tolerance){
  case already_been_tolerant_enough_is_enough:
    return wav2prg_false;
  default:
    if(functions->get_bit_func(context, functions, conf, bit) == wav2prg_false){
      if(state->tolerance == dont_be_tolerant)
        return wav2prg_false;
      state->tolerance = already_been_tolerant_enough_is_enough;
      *bit = 0;
    }
  }

  return wav2prg_true;
}

static enum wav2prg_bool ashdave_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t blocknum;
  struct ashdave_private_state *state = (struct ashdave_private_state *)conf->private_state;

  if(functions->get_byte_func(context, functions, conf, &blocknum) == wav2prg_false)
    return wav2prg_false;
  functions->number_to_name_func(blocknum, info->name);
  if(functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_word_func(context, functions, conf, &info->end  ) == wav2prg_false)
    return wav2prg_false;
  state->tolerance = be_tolerant;
  return wav2prg_true;
}

static const struct wav2prg_loaders ashdave_functions[] ={
  {
    "Ash & Dave",
    {
      ashdave_get_bit,
      NULL,
      NULL,
      NULL,
      ashdave_get_block_info,
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
      ashdave_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      0x80,
      sizeof(ashdave_pilot_sequence),
      ashdave_pilot_sequence,
      100,
      first_to_last,
      wav2prg_false,
      &ashdave_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(ashdave, 1, 0, "Ash & Dave loader", ashdave_functions)

