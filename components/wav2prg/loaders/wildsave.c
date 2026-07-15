/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * wildsave.c : decodes tapes in Wild Save, a format by Andrew Challis for Interceptor Micros
 * (called Wildload by Tapclean)
 */

#include "../wav2prg_api.h"

struct wildsave_private_state {
  enum wav2prg_bool synced_state;
};

static const struct wildsave_private_state wildsave_private_state_model = {
  wav2prg_false
};
static struct wav2prg_generate_private_state wildsave_generate_private_state = {
  sizeof(wildsave_private_state_model),
  &wildsave_private_state_model
};

static uint16_t wildsave_thresholds[]={480};
static uint8_t wildsave_pilot_sequence[]={10,9,8,7,6,5,4,3,2,1};

static enum wav2prg_sync_result wildsave_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  struct wildsave_private_state *state = (struct wildsave_private_state *)conf->private_state;
  if (state->synced_state)
    return wav2prg_sync_success;
  return functions->get_sync_sequence(context, functions, conf);
}

static enum wav2prg_bool wildsave_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint16_t size;
  uint8_t end_check_byte;
  struct wildsave_private_state *state = (struct wildsave_private_state *)conf->private_state;

  state->synced_state = wav2prg_true;

  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &size) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_byte_func(context, functions, conf, &end_check_byte) == wav2prg_false
   || end_check_byte == 0xFE || end_check_byte == 0xFF)
    return wav2prg_false;
  info->start = ++info->end - size;
  return wav2prg_true;
}

static uint8_t wildsave_postprocess_data_byte(struct wav2prg_plugin_conf *conf, uint8_t byte, uint16_t location)
{
  return byte ^ location;
}

static enum wav2prg_bool keep_doing_wildsave(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point)
{
  return wav2prg_true;
}

static const struct wav2prg_observers wildsave_observed_loaders[] = {
  {"Wild Save", {"Wild Save", NULL, keep_doing_wildsave}},
  {NULL, {NULL, NULL, NULL}}
};

static const struct wav2prg_loaders wildsave_functions[] = {
  {
    "Wild Save",
    {
      NULL,
      NULL,
      wildsave_sync,
      NULL,
      wildsave_get_block_info,
      NULL,
      NULL,
      NULL,
      wildsave_postprocess_data_byte
    },
    {
      lsbf,
      wav2prg_xor_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      2,
      wildsave_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      160,
      sizeof(wildsave_pilot_sequence),
      wildsave_pilot_sequence,
      0,
      last_to_first,
      wav2prg_false,
      &wildsave_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(wildsave, 1, 0, "Wild Save loader", wildsave_functions)
WAV2PRG_OBSERVER(wildsave, 1,0, wildsave_observed_loaders)

