/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * maddoctor.c : Mad Doctor tape
 */

#include "../wav2prg_api.h"

static uint16_t maddoctor_thresholds[]={384};
static int16_t maddoctor_pulse_length_deviations[]={0, 16};
static uint8_t maddoctor_sync_sequence[]={0xAA, 0xFF};

static enum wav2prg_sync_result maddoctor_get_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint8_t byte = 0;
  enum wav2prg_bool res;
  uint32_t i;
  uint8_t bit;

  res = functions->get_bit_func(context, functions, conf, &bit);
  if (res == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit == 0)
    return wav2prg_sync_failure;
  for(i = 0; i < conf->min_pilots; i++){
    res = functions->get_byte_func(context, functions, conf, &byte);
    if (res == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
    if (byte != 1)
      return wav2prg_sync_failure;
  }
  do{
    res = functions->get_byte_func(context, functions, conf, &byte);
    if (res == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
  }while(byte != 0xFF);
  return functions->get_sync_sequence(context, functions, conf);
}

static enum wav2prg_bool maddoctor_get_block(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct wav2prg_raw_block* block, uint16_t block_size)
{
  uint16_t bytes_received = 0;

  while(1) {
    if (!functions->get_block_func(context, functions, conf, block, 256))
      return wav2prg_false;
    bytes_received += 256;
    if (bytes_received >= block_size)
      return wav2prg_true;
    if (functions->check_checksum_func(context, functions, conf) != wav2prg_checksum_state_correct)
      return wav2prg_false;
  }
}

static enum wav2prg_bool recognize_maddoctor_hc(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  if (block->info.start == 828
   && block->info.end == 1020
   && block->data[0] == 1
   && block->data[1] == 1
   && block->data[2] == 8
   && block->data[4] == 8
   && block->data[0x35f - 0x33c] == 0xA9
   && block->data[0x361 - 0x33c] == 0x85
   && block->data[0x362 - 0x33c] == 0xFD
   && block->data[0x363 - 0x33c] == 0xA9
   && block->data[0x365 - 0x33c] == 0x85
   && block->data[0x366 - 0x33c] == 0xFB
   && block->data[0x367 - 0x33c] == 0xA9
   && block->data[0x369 - 0x33c] == 0x85
   && block->data[0x36A - 0x33c] == 0xFC
  ){
  	 uint16_t start = block->data[0x364 - 0x33c] + (block->data[0x368 - 0x33c] << 8);
    observer_functions->set_info_func(observer_context,
                         start,
                         start + (block->data[0x360 - 0x33c] << 8),
                         NULL);
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_maddoctor_self(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  uint16_t i;

  if(start_point == 0)
    for (i = 0; i < 144 && i + 2 < block->info.end - block->info.start; i++){
      if(block->data[i] == 0xCE
     && (block->data[i + 1] - 3) % 256 == (i + 1 + block->info.start) % 256
     &&  block->data[i + 2] == (block->info.start >> 8)
        ){
        start_point = i + 3;
        break;
      }
    }
  if(start_point == 0)
    return wav2prg_false;
  for (i = start_point; i < start_point + 10 && i + 5 < block->info.end - block->info.start; i++){
    if(block->data[i    ] == 0xA9
    && block->data[i + 2] == 0xA2
    && block->data[i + 4] == 0xA0
    ){
      uint16_t start = block->data[i + 3] + (block->data[i + 5] << 8);
      observer_functions->set_info_func(observer_context,
                                        start,
                                        start + (block->data[i + 1] << 8),
                                        NULL);
      observer_functions->set_restart_point_func(observer_context, i + 6);
      return wav2prg_true;
    }
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_creativesparks(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  uint16_t i;

  for (i = start_point; i + 11 < block->info.end - block->info.start; i++){
    if(block->data[i    ] == 0xA9
    && block->data[i + 2] == 0x85
    && block->data[i + 3] == 0xfd
    && block->data[i + 4] == 0xA9
    && block->data[i + 6] == 0x85
    && block->data[i + 7] == 0xfb
    && block->data[i + 8] == 0xA9
    && block->data[i + 10] == 0x85
    && block->data[i + 11] == 0xfc
    ){
      uint16_t start = block->data[i + 5] + (block->data[i + 9] << 8);
      observer_functions->set_info_func(observer_context,
                                        start,
                                        start + (block->data[i + 1] << 8),
                                        NULL);
      observer_functions->set_restart_point_func(observer_context, i + 12);
      return wav2prg_true;
    }
  }
  return wav2prg_false;
}

static const struct wav2prg_observers maddoctor_observed_loaders[] = {
  {"Default C64", {"Mad Doctor", NULL, recognize_maddoctor_hc}},
  {"Mad Doctor" , {"Mad Doctor", "Mad Doctor", recognize_maddoctor_self}},
  {"Mad Doctor" , {"Mad Doctor", "Creative Sparks", recognize_creativesparks}},
  {NULL,NULL}
};

static const struct wav2prg_loaders maddoctor_functions[] ={
  {
    "Mad Doctor",
    {
      NULL,
      NULL,
      maddoctor_get_sync,
      NULL,
      NULL,
      maddoctor_get_block,
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
      maddoctor_thresholds,
      maddoctor_pulse_length_deviations,
      wav2prg_custom_pilot_tone,
      0x55,/*ignored*/
      2,
      maddoctor_sync_sequence,
      15,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(maddoctor, 1, 1, "Mad Doctor loader", maddoctor_functions)
WAV2PRG_OBSERVER(maddoctor, 1,0, maddoctor_observed_loaders)

