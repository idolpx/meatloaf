/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * kernal.c : Kernal loader (the one built in ROM)
 */

#include "../wav2prg_api.h"

struct kernal_private_state {
  enum
  {
    byte_found,
    eof_marker_found,
    could_not_sync
  } result_of_last_byte;
};
static struct wav2prg_generate_private_state kernal_generate_private_state = {
  sizeof(struct kernal_private_state),
  NULL
};
struct headerchunk_private_state {
  struct kernal_private_state kernal_state;
  uint8_t headerchunk_necessary_bytes[21];
};
static struct wav2prg_generate_private_state headerchunk_generate_private_state = {
  sizeof(struct headerchunk_private_state),
  NULL
};

static uint16_t kernal_thresholds[]={426, 616};
static uint16_t kernal_c16_thresholds[]={640, 1260};
static uint8_t kernal_1stcopy_pilot_sequence[]={137,136,135,134,133,132,131,130,129};

static enum wav2prg_bool kernal_get_bit_func(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* bit)
{
  uint8_t pulse1, pulse2;
  if (functions->get_pulse_func(context, conf, &pulse1) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_pulse_func(context, conf, &pulse2) == wav2prg_false)
    return wav2prg_false;
  if (pulse1 == 0 && pulse2 == 1) {
    *bit = 0;
    return wav2prg_true;
  }
  if (pulse1 == 1 && pulse2 == 0) {
    *bit = 1;
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool sync_with_byte_and_get_it(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* byte, uint8_t allow_short_pulses_at_first)
{
  uint8_t pulse;
  uint8_t parity;
  uint8_t test;
  struct kernal_private_state *state = (struct kernal_private_state*)conf->private_state;

  while(1){
    if (functions->get_pulse_func(context, conf, &pulse) == wav2prg_false) {
      state->result_of_last_byte = could_not_sync;
      return wav2prg_false;
    }
    if(pulse==2)
      break;
    if(pulse!=0 || !allow_short_pulses_at_first) {
      state->result_of_last_byte = could_not_sync;
      return wav2prg_false;
    }
  }
  if (functions->get_pulse_func(context, conf, &pulse) == wav2prg_false) {
    state->result_of_last_byte = could_not_sync;
    return wav2prg_false;
  }
  switch(pulse){
  case 1:
    break;
  case 0:
    state->result_of_last_byte = eof_marker_found;
    return wav2prg_false;
  default:
    return could_not_sync;
  }
  if(functions->get_byte_func(context, functions, conf, byte) == wav2prg_false) {
    state->result_of_last_byte = could_not_sync;
    return wav2prg_false;
  }
  if(kernal_get_bit_func(context, functions, conf, &parity) == wav2prg_false) {
    state->result_of_last_byte = could_not_sync;
    return wav2prg_false;
  }
  for(test=1; test; test<<=1)
    parity^=(*byte & test)?1:0;
  state->result_of_last_byte = byte_found;
  return parity == 1;
}

enum wav2prg_bool kernal_get_first_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* byte)
{
  return sync_with_byte_and_get_it(context, functions, conf, byte, 1);
}

enum wav2prg_bool kernal_get_byte(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* byte)
{
  return sync_with_byte_and_get_it(context, functions, conf, byte, 0);
}

enum wav2prg_bool kernal_headerchunk_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t i;
  struct headerchunk_private_state *headerchunk_necessary_bytes = (struct headerchunk_private_state *)conf->private_state;
  const uint16_t num_of_necessary_bytes = sizeof(headerchunk_necessary_bytes->headerchunk_necessary_bytes);

  for(i = 0; i < num_of_necessary_bytes; i++){
    if(functions->get_data_byte_func(context, functions, conf, headerchunk_necessary_bytes->headerchunk_necessary_bytes + i, 0) == wav2prg_false)
      return wav2prg_false;
  }
  if (headerchunk_necessary_bytes->headerchunk_necessary_bytes[0] != 1
   && headerchunk_necessary_bytes->headerchunk_necessary_bytes[0] != 3)
    return wav2prg_false;
  for(i = 0; i < 16; i++)
    info->name[i] = headerchunk_necessary_bytes->headerchunk_necessary_bytes[i + 5];

  info->start=828;
  info->end=1020;

  return wav2prg_true;
}

/* This differs from the default implementation, functions->get_block_func(), because it is
   aware that sync_with_byte_and_get_it() can return eof_marker_found. In that case,
   the block is shorter than expected but still valid */
static enum wav2prg_bool kernal_get_block(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct wav2prg_raw_block *raw_block, uint16_t numbytes)
{
  uint16_t bytes_received = 0;
  uint8_t byte;
  struct kernal_private_state *state = (struct kernal_private_state*)conf->private_state;

  while(1){
    if(!functions->get_data_byte_func(context, functions, conf, &byte, 0))
      return (state->result_of_last_byte == eof_marker_found);
    if (bytes_received++ < numbytes)
      functions->add_byte_to_block_func(context, raw_block, byte);
  }
}

static enum wav2prg_bool kernal_headerchunk_get_block(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct wav2prg_raw_block *raw_block, uint16_t numbytes)
{
  struct headerchunk_private_state *headerchunk_necessary_bytes = (struct headerchunk_private_state *)conf->private_state;
  uint8_t i;

  for (i = 0; i < sizeof(headerchunk_necessary_bytes->headerchunk_necessary_bytes); i++)
    functions->add_byte_to_block_func(context, raw_block, headerchunk_necessary_bytes->headerchunk_necessary_bytes[i]);
  return kernal_get_block(context, functions, conf, raw_block, numbytes - i);
}

enum wav2prg_bool kernal_get_loaded_checksum(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* byte)
{
  *byte = 0;
  return wav2prg_true;
}

static enum wav2prg_bool is_headerchunk(struct wav2prg_observer_context *observer_context,
                                         const struct wav2prg_observer_functions *observer_functions,
                                         const struct program_block *block,
                                         uint16_t start_point)
{
  if(block->info.start == 828
  && block->info.end >= 849
  && block->info.end <= 1020
  && (block->data[0] == 1 || block->data[0] == 3)){
    uint16_t start = block->data[1] + (block->data[2] << 8);
    uint16_t end   = block->data[3] + (block->data[4] << 8);
    observer_functions->set_info_func(observer_context, start, end, NULL);
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool basic_header_second_copy_after_first_copy(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  uint8_t pilot_byte, i;
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  for(i = 0, pilot_byte = 9; i < conf->len_of_sync_sequence; i++, pilot_byte--){
    if (conf->sync_sequence[i] != kernal_1stcopy_pilot_sequence[i])
      return wav2prg_false;
    conf->sync_sequence[i] = pilot_byte;
  }
  return wav2prg_true;
}

static enum wav2prg_bool header_second_copy_after_first_copy(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  observer_functions->disallow_gaps_func(observer_context);

  return basic_header_second_copy_after_first_copy(observer_context,
                                                   observer_functions,
                                                   block,
                                                   start_point);
}

static enum wav2prg_bool data_second_copy_after_first_copy(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  observer_functions->set_info_func(observer_context, block->info.start, block->info.end, NULL);

  return header_second_copy_after_first_copy(observer_context,
                                             observer_functions,
                                             block,
                                             start_point);
}

static enum wav2prg_bool c16_data_second_copy_after_first_copy(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  observer_functions->set_info_func(observer_context, block->info.start, block->info.end, NULL);

  return basic_header_second_copy_after_first_copy(observer_context,
                                                   observer_functions,
                                                   block,
                                                   start_point);
}

enum wav2prg_bool kernal_headerchunk_16_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t byte;

  if(functions->get_data_byte_func(context, functions, conf, &byte, 0) == wav2prg_false)
    return wav2prg_false;
  if (byte != 1 && byte != 3)
    return wav2prg_false;
  info->start =  819;
  info->end   = 1010;

  return wav2prg_true;
}

static enum wav2prg_bool is_c16_headerchunk(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point)
{
  if(block->info.start == 819
  && block->info.end == 1010){
    uint16_t start = block->data[0] + (block->data[1] << 8);
    uint16_t end   = block->data[2] + (block->data[3] << 8);
    observer_functions->set_info_func(observer_context, start, end, block->data + 4);
    return wav2prg_true;
  }
  return wav2prg_false;
}

const struct wav2prg_loaders kernal_one_loader[] =
{
  {
    "Default C64",
    {
      kernal_get_bit_func,
      kernal_get_byte,
      NULL,
      kernal_get_first_sync,
      kernal_headerchunk_get_block_info,
      kernal_headerchunk_get_block,
      NULL,
      kernal_get_loaded_checksum,
      NULL
    },
    {
      lsbf,
      wav2prg_xor_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      3,
      kernal_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,/*ignored, overriding get_sync_byte*/
      0,/*ignored, overriding get_sync_byte*/
      9,
      kernal_1stcopy_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      &headerchunk_generate_private_state
    }
  },
  {
    "Kernal data chunk",
    {
      kernal_get_bit_func,
      kernal_get_byte,
      NULL,
      kernal_get_first_sync,
      NULL,/* info is in header chunk */
      kernal_get_block,
      NULL,
      kernal_get_loaded_checksum,
      NULL
    },
    {
      lsbf,
      wav2prg_xor_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      3,
      kernal_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,/*ignored, overriding get_sync_byte*/
      0,/*ignored, overriding get_sync_byte*/
      9,
      kernal_1stcopy_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      &kernal_generate_private_state
    }
  },
  {
    "Default C16",
    {
      kernal_get_bit_func,
      kernal_get_byte,
      NULL,
      kernal_get_first_sync,
      kernal_headerchunk_16_get_block_info,
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      lsbf,
      wav2prg_xor_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      3,
      kernal_c16_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,/*ignored, overriding get_sync_byte*/
      0,/*ignored, overriding get_sync_byte*/
      9,
      kernal_1stcopy_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      &kernal_generate_private_state
    }
  },
  {
    "Kernal data chunk C16",
    {
      kernal_get_bit_func,
      kernal_get_byte,
      NULL,
      kernal_get_first_sync,
      NULL,/*recognize_block_as_mine_with_start_end_func is not NULL */
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      lsbf,
      wav2prg_xor_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      3,
      kernal_c16_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,/*ignored, overriding get_sync_byte*/
      0,/*ignored, overriding get_sync_byte*/
      9,
      kernal_1stcopy_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      &kernal_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(kernal,1,0,"Kernal plug-in for WAV-PRG 4.0", kernal_one_loader)

static const struct wav2prg_observers kernal_observers[] = {
  {"Default C64", {"Default C64","2nd copy",  header_second_copy_after_first_copy}},
  {"Default C64", {"Kernal data chunk", NULL, is_headerchunk}},
  {"Kernal data chunk", {"Kernal data chunk", "2nd copy", data_second_copy_after_first_copy}},
  {"Default C16", {"Default C16", "2nd copy", basic_header_second_copy_after_first_copy}},
  {"Default C16", {"Kernal data chunk C16", NULL, is_c16_headerchunk}},
  {"Kernal data chunk C16", {"Kernal data chunk C16", "2nd copy", c16_data_second_copy_after_first_copy}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(kernal, 1,0, kernal_observers)
