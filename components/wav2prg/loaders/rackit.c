/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * rackit.c : format found in many Rack-It games
 */

#include "../wav2prg_api.h"

struct rackit_private_state {
  uint8_t xor_value_present;
  uint8_t xor_value;
  uint8_t checksum;
};

static const struct rackit_private_state rackit_private_state_model = {
  1,
  0x77,
  0
};
static struct wav2prg_generate_private_state rackit_generate_private_state = {
  sizeof(rackit_private_state_model),
  &rackit_private_state_model
};

static uint16_t rackit_thresholds[]={352};
static uint8_t rackit_pilot_sequence[]={0x3D};

static enum wav2prg_bool rackit_get_loaded_checksum(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* loaded_checksum)
{
  struct rackit_private_state* state = (struct rackit_private_state*)conf->private_state;

  *loaded_checksum = state->checksum;
  return wav2prg_true;
}

static uint8_t rackit_postprocess_data_byte(struct wav2prg_plugin_conf *conf, uint8_t byte, uint16_t location)
{
  struct rackit_private_state* state = (struct rackit_private_state*)conf->private_state;
  return byte ^ state->xor_value;
}

static enum wav2prg_bool rackit_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  struct rackit_private_state* state = (struct rackit_private_state*)conf->private_state;
  uint8_t byte;
  
  if (state->xor_value_present){
    if (functions->get_byte_func(context, functions, conf, &state->xor_value) == wav2prg_false)
      return wav2prg_false;
  }  
  else
    state->xor_value = 0;
    
  if(functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_byte_func(context, functions, conf, &state->checksum) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_word_bigendian_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_word_bigendian_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
    return wav2prg_false;
  functions->number_to_name_func(byte, info->name);
  if(conf->endianness == lsbf)
    functions->reset_checksum_to_func(context, 0x80);
  return wav2prg_true;
}

static enum wav2prg_bool is_rackit(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct rackit_private_state* state = (struct rackit_private_state*)conf->private_state;
  const uint8_t xor_bytes[]={0x98,0xe8,0x60,0x08,0x98,0xec,0xb4,0x04,0x08,0x24,0xe8,0xc0,
                             0x28,0x24,0x80,0xc0,0xbc,0xe0,0xa4,0xc0,0xe0,0xa4,0x40,0x80};
  uint8_t xor_byte;
  uint16_t i;
  const uint16_t start_first_part = 234, end_first_part = 403, start_second_part = 423;

  if (block->info.start != 0x316 || block->info.end < 0x570)
    return wav2prg_false;

  for(i = start_first_part; i < end_first_part; i++){
    if (block->data[i   ] == 0xad
     && block->data[i+ 1] == 0x00
     && block->data[i+ 2] == 0x80
     && block->data[i+ 3] == 0xee
     && block->data[i+ 4] == 0x00
     && block->data[i+ 5] == 0x80
     && block->data[i+ 6] == 0xcd
     && block->data[i+ 7] == 0x00
     && block->data[i+ 8] == 0x80
     && block->data[i+ 9] == 0x26
     && block->data[i+11] == 0x68
     && block->data[i+12] == 0xc9
     && block->data[i+13] == 0x61
     && block->data[i+14] == 0xf0
     && block->data[i+15] == 0x01
     && block->data[i+16] == 0x18
     && block->data[i+17] == 0x26
     && block->data[i+18] >= 0x73
     && block->data[i+18] <= 0x8a
     && block->data[i+18] == block->data[i+20]
     && block->data[i+18] == block->data[i+10]
     && block->data[i+19] == 0xa5
    ){
      xor_byte=xor_bytes[block->data[i+18]-0x73];
      break;
    }
  }
  if (i == end_first_part)
    return wav2prg_false;

  for(i = start_second_part;
      i < block->info.end - block->info.start - 23;
      i++){
    if ((block->data[i   ] ^ xor_byte) == 0xc9
     &&  block->data[i+ 1] == block->data[i+19]
     && (block->data[i+ 2] ^ xor_byte) == 0xd0
     && (block->data[i+ 4] ^ xor_byte) == 0xa2
     && (block->data[i+ 6] ^ xor_byte) == 0x86
     && (block->data[i+ 7] ^ xor_byte) == 0xff
     && (block->data[i+ 8] ^ xor_byte) == 0xa9
     && (block->data[i+10] ^ xor_byte) == 0xd0
     && (block->data[i+12] ^ xor_byte) == 0x90
     && (block->data[i+14] ^ xor_byte) == 0xa2
     &&  block->data[i+15] == block->data[i+ 5]
     && (block->data[i+16] ^ xor_byte) == 0x86
     && (block->data[i+17] ^ xor_byte) == 0xff
     && (block->data[i+18] ^ xor_byte) == 0xc9
     && (block->data[i+20] ^ xor_byte) == 0xf0
     && (block->data[i+22] ^ xor_byte) == 0xc9
    ){
      uint16_t where_to_check_xor_byte;

      switch(block->data[i+ 5] ^ xor_byte){
      case 0x01:
        conf->endianness = msbf;
        where_to_check_xor_byte = i + 30;
        break;
      case 0x80:
        conf->endianness = lsbf;
        where_to_check_xor_byte = i + 29;
        break;
      default:
        continue;
      }
      switch (block->data[where_to_check_xor_byte] ^ xor_byte){
      case 0x08:
        state->xor_value_present = 1;
        break;
      case 0x07:
        state->xor_value_present = 0;
        break;
      default:
        continue;
      }

      conf->pilot_byte       = block->data[i+ 1] ^ xor_byte;
      conf->sync_sequence[0] = block->data[i+23] ^ xor_byte;
      return wav2prg_true;
    }
  }
  return wav2prg_false;
}

static enum wav2prg_bool keep_doing_rackit(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  return (block->info.start != 0xfffc || block->info.end != 0xfffe);
}

static const struct wav2prg_observers rackit_observed_loaders[] = 
{
  {"Kernal data chunk", {"Rack-It", NULL, is_rackit}},
  {"Rack-It"          , {"Rack-It", NULL, keep_doing_rackit}},
  {NULL,NULL}
};

static const struct wav2prg_loaders rackit_functions[] =
{
  {
    "Rack-It",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      rackit_get_block_info,
      NULL,
      NULL,
      rackit_get_loaded_checksum,
      rackit_postprocess_data_byte
    },
    {
      msbf,
      wav2prg_xor_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      2,
      rackit_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      0x25,
      sizeof(rackit_pilot_sequence),
      rackit_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      &rackit_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(rackit, 1, 1, "Rack-It loader", rackit_functions)
WAV2PRG_OBSERVER(rackit, 1,0, rackit_observed_loaders)
