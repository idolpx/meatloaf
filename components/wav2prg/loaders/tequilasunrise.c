/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * tequilasunrise.c : decodes Tequila Sunrise
 */

#include "../wav2prg_api.h"

static uint16_t tequilasunrise_thresholds[]={263};
static uint8_t tequilasunrise_pilot_sequence[]={9,8,7,6,5,4,3,2,1};

struct tequilasunrise_private_state {
  uint8_t wraparound_bytes;
};
struct tequilasunrise_private_state tequilasunrise_private_state_model = {0};
static struct wav2prg_generate_private_state tequilasunrise_generate_private_state = {
  sizeof(struct tequilasunrise_private_state),
  &tequilasunrise_private_state_model
};

static enum wav2prg_bool tequilasunrise_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t byte;
  struct tequilasunrise_private_state *state =(struct tequilasunrise_private_state *)conf->private_state;

  if (!functions->get_byte_func(context, functions, conf, &byte))
    return wav2prg_false;
  if (byte == 0)
    return wav2prg_false;
  functions->number_to_name_func(byte, info->name);
  conf->endianness = lsbf;
  if (!functions->get_word_func(context, functions, conf, &info->start))
    return wav2prg_false;
  if (!functions->get_word_func(context, functions, conf, &info->end))
    return wav2prg_false;
  if (info->end == 2)
  {
    state->wraparound_bytes = info->end;
    info->end = 0;
  }
  
  return wav2prg_true;
}

static enum wav2prg_bool tequilasunrise_get_block(struct wav2prg_context *context, const struct wav2prg_functions *functions, struct wav2prg_plugin_conf *conf, struct wav2prg_raw_block *block, uint16_t size)
{
  uint16_t entry_point;
  enum wav2prg_bool retval;
  struct tequilasunrise_private_state *state =(struct tequilasunrise_private_state *)conf->private_state;
  uint8_t wraparound_bytes;

  if (!functions->get_block_func(context, functions, conf, block, size))
    return wav2prg_false;
  for (wraparound_bytes = 0; wraparound_bytes < state->wraparound_bytes; wraparound_bytes++)
  {
    uint8_t wraparound_byte;
    if (!functions->get_byte_func(context, functions, conf, &wraparound_byte))
      return wav2prg_false;
  }
  retval = functions->get_word_bigendian_func(context, functions, conf, &entry_point);
  entry_point++;
  return retval;
}

static const struct wav2prg_loaders tequilasunrise[] =
{
  {
    "Tequila Sunrise",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      tequilasunrise_get_block_info,
      tequilasunrise_get_block,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_xor_checksum,/*ignored*/
      wav2prg_do_not_compute_checksum,
      0,
      2,
      tequilasunrise_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      2,
      sizeof(tequilasunrise_pilot_sequence),
      tequilasunrise_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      &tequilasunrise_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(tequilasunrise,1,0,"Tequila Sunrise loader", tequilasunrise);
