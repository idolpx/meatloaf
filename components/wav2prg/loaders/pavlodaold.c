/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * pavlodaold.c : an old format defined as Pavloda by Tapclean
 */

#include "../wav2prg_api.h"

static uint16_t pavlodaold_thresholds[]={0x14A};

struct pavlodaold_private_state {
  enum{
    dont_be_tolerant,
    be_tolerant,
    already_been_tolerant_enough_is_enough
  } tolerance;
};

static const struct pavlodaold_private_state pavloda_private_state_model = {
  dont_be_tolerant
};
static struct wav2prg_generate_private_state pavloda_generate_private_state = {
  sizeof(pavloda_private_state_model),
  &pavloda_private_state_model
};

static uint8_t pavlodaold_compute_checksum_step(struct wav2prg_plugin_conf* conf, uint8_t old_checksum, uint8_t byte, uint16_t location_of_byte, uint32_t *extended_checksum) {
  return old_checksum + byte + 1;
}

static enum wav2prg_bool pavlodaold_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  if(functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_word_func(context, functions, conf, &info->end  ) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static enum wav2prg_bool pavlodaold_get_bit(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* bit) {
  uint8_t pulse;
  struct pavlodaold_private_state *state = (struct pavlodaold_private_state *)conf->private_state;

  if(state->tolerance == already_been_tolerant_enough_is_enough)
    return wav2prg_false;

  if(functions->get_pulse_func(context, conf, &pulse) == wav2prg_false){
    if(state->tolerance == dont_be_tolerant)
      return wav2prg_false;
    pulse = 1;
    state->tolerance = already_been_tolerant_enough_is_enough;
  }

  if (pulse == 0){
    if(functions->get_pulse_func(context, conf, &pulse) == wav2prg_false){
      if(state->tolerance == dont_be_tolerant)
        return wav2prg_false;
      state->tolerance = already_been_tolerant_enough_is_enough;
    }
    *bit = 1;
  }
  else
    *bit = 0;

  return wav2prg_true;
}

static enum wav2prg_bool pavlodaold_get_block_func(struct wav2prg_context *context, const struct wav2prg_functions *functions, struct wav2prg_plugin_conf *conf, struct wav2prg_raw_block *block, uint16_t len)
{
  struct pavlodaold_private_state *state = (struct pavlodaold_private_state *)conf->private_state;
  enum wav2prg_bool result;

  state->tolerance = dont_be_tolerant;
  result = functions->get_block_func(context, functions, conf, block, len);
  state->tolerance = be_tolerant;

  return result;
}

static const struct wav2prg_loaders pavlodaold_functions[] ={
  {
    "Pavloda Old",
    {
      pavlodaold_get_bit,
      NULL,
      NULL,
      NULL,
      pavlodaold_get_block_info,
      pavlodaold_get_block_func,
      pavlodaold_compute_checksum_step,
      NULL
    },
    {
      msbf,
      wav2prg_xor_checksum,/*ignored, compute_checksum_step overridden*/
      wav2prg_compute_and_check_checksum,
      0,
      2,
      pavlodaold_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
      0,/*ignored, no sync sequence*/
      0,
      NULL,
      514,
      first_to_last,
      wav2prg_false,
      &pavloda_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(pavlodaold, 1, 0, "Pavloda (earliest version)", pavlodaold_functions)
