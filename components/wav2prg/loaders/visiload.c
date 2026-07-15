/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * visiload.c : Visiload
 */

#include "../wav2prg_api.h"

struct visiload_private_state {
  enum wav2prg_bool synced_state;
  uint8_t additional_pulses_in_byte;
  uint8_t additional_bytes_in_header;
};

static const struct visiload_private_state visiload_private_state_model = {
  wav2prg_false,
  1,
  0
};
static struct wav2prg_generate_private_state visiload_generate_private_state = {
  sizeof(visiload_private_state_model),
  &visiload_private_state_model
};

static uint16_t visiload_thresholds[]={0x1b6};
static uint8_t visiload_sync_sequence[]={0x16};

static enum wav2prg_sync_result visiload_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  struct visiload_private_state *state = (struct visiload_private_state *)conf->private_state;

  if (state->synced_state)
    return wav2prg_sync_success;
  
  return functions->get_sync_sequence(context, functions, conf);
}

static enum wav2prg_bool visiload_get_byte(struct wav2prg_context *context, const struct wav2prg_functions *functions, struct wav2prg_plugin_conf *conf, uint8_t *byte)
{
  struct visiload_private_state *state = (struct visiload_private_state *)conf->private_state;
  uint8_t i;

  for (i = 0; i < state->additional_pulses_in_byte; i++)
  {
    uint8_t useless_bit;
    if (!functions->get_bit_func(context, functions, conf, &useless_bit))
      return wav2prg_false;
  }
  return functions->get_byte_func(context, functions, conf, byte);
}

static enum wav2prg_bool visiload_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t additional_bytes;
  struct visiload_private_state *state = (struct visiload_private_state *)conf->private_state;

  state->synced_state = wav2prg_true;

  for (additional_bytes = 0; additional_bytes < state->additional_bytes_in_header; additional_bytes++)
  {
    uint8_t useless_byte;
    if (!visiload_get_byte(context, functions, conf, &useless_byte))
      return wav2prg_false;
  }
  if (functions->get_word_bigendian_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_bigendian_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (info->end == info->start)
    info->end++;
  return wav2prg_true;
}

static enum wav2prg_bool recognize_visiload(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point)
{
  if (block->info.start == 0x29f
    && block->info.end == 0x3c0
    && block->data[0x378 - 0x29f] == 0xa9
    && block->data[0x37a - 0x29f] == 0x8d
    && block->data[0x37b - 0x29f] == 0x4
    && block->data[0x37c - 0x29f] == 0xdd
    && block->data[0x37d - 0x29f] == 0xa9
    && block->data[0x37f - 0x29f] == 0x8d
    && block->data[0x380 - 0x29f] == 0x5
    && block->data[0x381 - 0x29f] == 0xdd){
      struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
      conf->thresholds[0] = block->data[0x379 - 0x29f] + (block->data[0x37e - 0x29f] << 8);
      return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool keep_doing_visiload(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point)
{
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct visiload_private_state *state = (struct visiload_private_state *)conf->private_state;

  if (block->info.start == 0x034b){
    if ((state->additional_pulses_in_byte = block->data[0]) < 8)
      return wav2prg_false;
    state->additional_pulses_in_byte -= 8;
  }
  else if (block->info.start == 0x03a4){
    if ((state->additional_bytes_in_header = block->data[0]) < 3)
      return wav2prg_false;
    state->additional_bytes_in_header -= 3;
  }
  else if (block->info.start == 0x0347){
    if (block->data[0] == 0x26)
      conf->endianness = msbf;
    else if (block->data[0] == 0x66)
      conf->endianness = lsbf;
    else
      return wav2prg_false;
  }
  else if (block->info.start == 0x03bb){
    if (block->info.end == 0x3db){
      if ((block->data[20]!=0x60 || block->data[22]!=0x45)
       && (block->data[20]!=0x36 || block->data[22]!=0xc7)
       && (block->data[20]!=0    || block->data[22]!=0x80)
       && (block->data[20]!=0    || block->data[22]!=0xd0)
       && (block->data[20]!=0    || block->data[22]!=0x48)
       && (block->data[20]!=0x41 || block->data[22]!=0xff)
       && (block->data[20]!=0    || block->data[22]!=0xc4)
      )
        return wav2prg_false;
    }
    state->synced_state = wav2prg_false;
  }
  return wav2prg_true;
}

static const struct wav2prg_observers visiload_observed_loaders[] = {
  {"Visiload", {"Visiload", NULL, keep_doing_visiload}},
  {"Kernal data chunk", {"Visiload", NULL, recognize_visiload}},
  {NULL, {NULL, NULL, NULL}}
};

static const struct wav2prg_loaders visiload_functions[] = {
  {
    "Visiload",
    {
      NULL,
      visiload_get_byte,
      visiload_sync,
      NULL,
      visiload_get_block_info,
      NULL,
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
      visiload_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      0,/*ignored*/
      sizeof(visiload_sync_sequence),
      visiload_sync_sequence,
      0,
      first_to_last,
      wav2prg_false,
      &visiload_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(visiload, 1, 0, "Wild Save loader", visiload_functions)
WAV2PRG_OBSERVER(visiload, 1,0, visiload_observed_loaders)

