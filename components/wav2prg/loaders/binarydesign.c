/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * binarydesign.c : format found in some Virgin and Hi-Tec games
 */

#include "../wav2prg_api.h"

static uint16_t binarydesign_thresholds[]={0x15e};
static uint8_t binarydesign_pilot_sequence[]={0xa0};

static enum wav2prg_bool binarydesign_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t blocknum;
  uint16_t entry_point;

  if(functions->get_byte_func(context, functions, conf, &blocknum) == wav2prg_false)
    return wav2prg_false;
  functions->number_to_name_func(blocknum, info->name);
  if(functions->get_byte_func(context, functions, conf, &blocknum) == wav2prg_false)
    return wav2prg_false;/*unused*/
  if(functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_word_func(context, functions, conf, &info->end  ) == wav2prg_false)
    return wav2prg_false;
  if(functions->get_word_func(context, functions, conf, &entry_point) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static const struct wav2prg_loaders binarydesign_functions[] ={
  {
    "Binary Design",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      binarydesign_get_block_info,
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
      binarydesign_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      0xaa,
      sizeof(binarydesign_pilot_sequence),
      binarydesign_pilot_sequence,
      100,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(binarydesign, 1, 0, "Binary Design loader", binarydesign_functions)

static enum wav2prg_bool recognize_virgin(struct wav2prg_observer_context *observer_context,
                                           const struct wav2prg_observer_functions *observer_functions,
                                           const struct program_block *block,
                                           uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x2a7
   && block->info.end == 0x304
   && block->data[0x2ac -0x2a7] == 0xA9
   && block->data[0x2ae -0x2a7] == 0x8D
   && block->data[0x2af -0x2a7] == 0x06
   && block->data[0x2b0 -0x2a7] == 0xDC
   && block->data[0x2b1 -0x2a7] == 0xA0
   && block->data[0x2b3 -0x2a7] == 0x8C
   && block->data[0x2b4 -0x2a7] == 0x07
   && block->data[0x2b5 -0x2a7] == 0xDC
   && block->data[0x2fc -0x2a7] == 0x60
  ){
    conf->thresholds[0] = (block->data[0x2b2 - 0x2a7] << 8) + block->data[0x2ad - 0x2a7];
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_hitec(struct wav2prg_observer_context *observer_context,
                                           const struct wav2prg_observer_functions *observer_functions,
                                           const struct program_block *block,
                                           uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x2a7
   && block->info.end == 0x304
   && block->data[0x2bc -0x2a7] == 0xA9
   && block->data[0x2be -0x2a7] == 0x8D
   && block->data[0x2bf -0x2a7] == 0x06
   && block->data[0x2c0 -0x2a7] == 0xDC
   && block->data[0x2c1 -0x2a7] == 0xA0
   && block->data[0x2c3 -0x2a7] == 0x8C
   && block->data[0x2c4 -0x2a7] == 0x07
   && block->data[0x2c5 -0x2a7] == 0xDC
   && block->data[0x2ff -0x2a7] == 0x60
  ){
    conf->thresholds[0] = (block->data[0x2c2 - 0x2a7] << 8) + block->data[0x2bd - 0x2a7];
    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers binarydesign_observers[] = {
  {"Kernal data chunk", {"Binary Design", "Virgin", recognize_virgin}},
  {"Kernal data chunk", {"Binary Design", "Hi-Tec", recognize_hitec}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(binarydesign, 1,0, binarydesign_observers)

