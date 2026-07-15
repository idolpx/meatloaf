/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * snakeload.c : decodes tapes in Snakeload v5.0, a format by Steve Snake
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool snakeload_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t fileid;

  if (functions->get_byte_func(context, functions, conf, &fileid) == wav2prg_false)
    return wav2prg_false;
  functions->number_to_name_func(fileid, info->name);
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static uint16_t snakeload_thresholds[]={0x3ff};
static uint8_t snakeload_pilot_sequence[]={'e','i','l','y','K'};

static enum wav2prg_bool recognize_snakeload(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  uint16_t i, blocklen = block->info.end - block->info.start;
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start != 0x801)
    return wav2prg_false;

  for (i = 0; i + 9 < blocklen; i++){
    if (block->data[i    ] == 0xa9
     && block->data[i + 2] == 0x8d
     && block->data[i + 3] == 0x04
     && block->data[i + 4] == 0xdc
     && block->data[i + 5] == 0xa9
     && block->data[i + 7] == 0x8d
     && block->data[i + 8] == 0x05
     && block->data[i + 9] == 0xdc
    ){
      conf->thresholds[0] = block->data[i + 1] + (block->data[i + 6] << 8) - 0x200;
      break;
    }
  }
  if(i + 9 == blocklen)
    return wav2prg_false;
  for (; i + 4 < blocklen; i++)
    if (block->data[i    ] == 'K'
     && block->data[i + 1] == 'y'
     && block->data[i + 2] == 'l'
     && block->data[i + 3] == 'i'
     && block->data[i + 4] == 'e'
    )
      return wav2prg_true;
  return wav2prg_false;
}

static const struct wav2prg_observers snakeload_observed_loaders[] = {
  {"Kernal data chunk", {"Snakeload", NULL, recognize_snakeload}},
  {NULL, {NULL,NULL, NULL}}
};

static const struct wav2prg_loaders snakeload_functions[] = {
  {
    "Snakeload",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      snakeload_get_block_info,
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_add_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      2,
      snakeload_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
      64 /*ignored*/,
      sizeof(snakeload_pilot_sequence),
      snakeload_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(snakeload, 1, 0, "Steve Snake loader with 5-byte sync sequence (called Snakeload v5.0 in Ninja Warriors code)", snakeload_functions)
WAV2PRG_OBSERVER(snakeload, 1,0, snakeload_observed_loaders)
