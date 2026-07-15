/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * jedi.c : detects Return of the Jedi (variant of Flashload)
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool jedi_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t useless_byte;

  if (functions->get_byte_func(context, functions, conf, &useless_byte) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  info->end++;
  return wav2prg_true;
}

static uint16_t jedi_thresholds[]={0x118};
static uint8_t jedi_pilot_sequence[]={0x33};

static const struct wav2prg_loaders jedi_functions[] = {
  {
    "Jedi",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      jedi_get_block_info,
      NULL,
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
      jedi_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_1_bits_followed_by_0,
      64,
      sizeof(jedi_pilot_sequence),
      jedi_pilot_sequence,
      256,
      first_to_last,
      wav2prg_false,
      NULL
    },
  },
  {NULL}
};

WAV2PRG_LOADER(jedi, 1, 0, "Return of the Jedi plug-in", jedi_functions)

static enum wav2prg_bool recognize_jedi(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0xc100
   && block->info.end == 0xc1d8
   && block->data[0x50] == 0xc9
   && block->data[0x52] == 0xd0
   && block->data[0x53] == 0xe7
   && block->data[0x2c] == 0xa9
   && block->data[0x2e] == 0x8d
   && block->data[0x2f] == 0x04
   && block->data[0x30] == 0xdd
   && block->data[0x31] == 0xa9
   && block->data[0x33] == 0x8d
   && block->data[0x34] == 0x05
   && block->data[0x35] == 0xdd
  ){
    conf->thresholds[0] = block->data[0x2d] + (block->data[0x32] << 8);
    conf->sync_sequence[0] = block->data[0x51];
    return wav2prg_true;
  }
  return wav2prg_false;
}
static enum wav2prg_bool recognize_jedi_loader(struct wav2prg_observer_context *observer_context,
                                           const struct wav2prg_observer_functions *observer_functions,
                                           const struct program_block *block,
                                           uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x2d0
   && block->info.end == 0x304
   && block->data[0x2df - 0x2d0] == 0xA9
   && block->data[0x2e1 - 0x2d0] == 0x85
   && block->data[0x2e2 - 0x2d0] == 0xc1
   && block->data[0x2e3 - 0x2d0] == 0xA9
   && block->data[0x2e5 - 0x2d0] == 0x85
   && block->data[0x2e6 - 0x2d0] == 0xc2
   && block->data[0x2e7 - 0x2d0] == 0xA9
   && block->data[0x2e9 - 0x2d0] == 0x85
   && block->data[0x2ea - 0x2d0] == 0xae
   && block->data[0x2eb - 0x2d0] == 0xA9
   && block->data[0x2ed - 0x2d0] == 0x85
   && block->data[0x2ee - 0x2d0] == 0xaf
  ){
    int i;

    observer_functions->set_info_func(observer_context,
                                      block->data[0x2e0 - 0x2d0] + (block->data[0x2e4 - 0x2d0] << 8),
                                      block->data[0x2e8 - 0x2d0] + (block->data[0x2ec - 0x2d0] << 8),
                                      NULL);
    for(i = 0; i < 9; i++)
      conf->sync_sequence[i] = 137-i;

    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers jedi_observers[] = {
  {"Kernal data chunk", {"Jedi", NULL, recognize_jedi}},
  {"Kernal data chunk", {"Kernal data chunk", "Jedi", recognize_jedi_loader}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(jedi, 1,0, jedi_observers)
