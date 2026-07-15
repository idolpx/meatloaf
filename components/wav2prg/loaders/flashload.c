/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * flashload.c : detects Flashload (used on some Activision games and more)
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool flashload_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  info->end++;
  return wav2prg_true;
}

static uint16_t flashload_thresholds[]={0x118};
static uint8_t flashload_pilot_sequence[]={0x33};

static const struct wav2prg_loaders flashload_functions[] = {
  {
    "Flashload",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      flashload_get_block_info,
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
      flashload_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_1_bits_followed_by_0,
      64,
      sizeof(flashload_pilot_sequence),
      flashload_pilot_sequence,
      256,
      first_to_last,
      wav2prg_false,
      NULL
    },
  },
  {NULL}
};

WAV2PRG_LOADER(flashload, 1, 0, "Flashload plug-in", flashload_functions)

static enum wav2prg_bool recognize_flashload(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0xce00
   && block->info.end == 0xcfff
   && (block->data[0x3c] == (uint8_t)((0xc9 ^ 0xff) + 0x21))
   && (block->data[0x3e] == (uint8_t)((0xd0 ^ 0xff) + 0x21))
   && (block->data[0x3f] == (uint8_t)((0xe7 ^ 0xff) + 0x21))
   && (block->data[0x18] == (uint8_t)((0xa9 ^ 0xff) + 0x21))
   && (block->data[0x1a] == (uint8_t)((0x8d ^ 0xff) + 0x21))
   && (block->data[0x1b] == (uint8_t)((0x04 ^ 0xff) + 0x21))
   && (block->data[0x1c] == (uint8_t)((0xdd ^ 0xff) + 0x21))
   && (block->data[0x1d] == (uint8_t)((0xa9 ^ 0xff) + 0x21))
   && (block->data[0x1f] == (uint8_t)((0x8d ^ 0xff) + 0x21))
   && (block->data[0x20] == (uint8_t)((0x05 ^ 0xff) + 0x21))
   && (block->data[0x21] == (uint8_t)((0xdd ^ 0xff) + 0x21))
  ){
    conf->thresholds[0] = (((block->data[0x19] ^ 0xff) + 0x21) & 0xff)
                        + (((block->data[0x1e] ^ 0xff) + 0x21) << 8);
    conf->sync_sequence[0] = (block->data[0x3D] ^ 0xff) + 0x21;
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_fp_flashload(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x316
   && block->info.end == 0x412
   && block->data[0x348 - 0x316] == 0xa9
   && block->data[0x34a - 0x316] == 0x8d
   && block->data[0x34b - 0x316] == 0x06
   && block->data[0x34c - 0x316] == 0xdd
   && block->data[0x34d - 0x316] == 0xa9
   && block->data[0x34f - 0x316] == 0x8d
   && block->data[0x350 - 0x316] == 0x07
   && block->data[0x351 - 0x316] == 0xdd
  ){
    conf->thresholds[0] = block->data[0x349 - 0x316] + (block->data[0x34e - 0x316] << 8);
    conf->len_of_sync_sequence = 0;
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_flashload_loader(struct wav2prg_observer_context *observer_context,
                                           const struct wav2prg_observer_functions *observer_functions,
                                           const struct program_block *block,
                                           uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x316
   && block->info.end == 1045
   && block->data[0x363 -0x316] == 0xA9
   && block->data[0x365 -0x316] == 0x85
   && block->data[0x366 -0x316] == 0xc1
   && block->data[0x367 -0x316] == 0xA9
   && block->data[0x369 -0x316] == 0x85
   && block->data[0x36a -0x316] == 0xae
   && block->data[0x36b -0x316] == 0xA9
   && block->data[0x36d -0x316] == 0x85
   && block->data[0x36e -0x316] == 0xc2
   && block->data[0x36f -0x316] == 0xA9
   && block->data[0x371 -0x316] == 0x85
   && block->data[0x372 -0x316] == 0xaf
  ){
    int i;

    observer_functions->set_info_func(observer_context,
                                      block->data[0x364 -0x316] + (block->data[0x36c -0x316] << 8),
                                      block->data[0x368 -0x316] + (block->data[0x370 -0x316] << 8),
                                      NULL);
    for(i = 0; i < 9; i++)
      conf->sync_sequence[i] = 137-i;

    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers flashload_observers[] = {
  {"Kernal data chunk", {"Flashload", NULL, recognize_flashload}},
  {"Kernal data chunk", {"Flashload", "Falcon Patrol II variant", recognize_fp_flashload}},
  {"Kernal data chunk", {"Kernal data chunk", "Flashload", recognize_flashload_loader}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(flashload, 1,0, flashload_observers)
