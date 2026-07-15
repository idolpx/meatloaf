/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * chr.c : decodes Cauldron, Hewson and Rainbird formats
 */

#include "../wav2prg_api.h"

struct chr_private_state {
  uint8_t header[6];
};
static struct wav2prg_generate_private_state chr_generate_private_state = {
  sizeof(struct chr_private_state),
  NULL
};

static uint16_t chr_thresholds[]={263};
static uint8_t chr_pilot_sequence[]={0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b,
                                     0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73,
                                     0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b,
                                     0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83,
                                     0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b,
                                     0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93,
                                     0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b,
                                     0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3,
                                     0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab,
                                     0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
                                     0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb,
                                     0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3,
                                     0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb,
                                     0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3,
                                     0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb,
                                     0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3,
                                     0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb,
                                     0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3,
                                     0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb,
                                     0xfc, 0xfd, 0xfe, 0xff};

static enum wav2prg_bool chr_specialagent_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  struct chr_private_state *state =(struct chr_private_state *)conf->private_state;
  uint8_t byte;
  int i;

  if (!functions->get_byte_func(context, functions, conf, &byte))
    return wav2prg_false;
  if (byte == 0)
    return wav2prg_false;
  if (!functions->get_word_func(context, functions, conf, &info->start))
    return wav2prg_false;
  if (!functions->get_word_func(context, functions, conf, &info->end))
    return wav2prg_false;
  for (i = 0; i < 6; i++)
  {
    if (!functions->get_byte_func(context, functions, conf, state->header + i))
      return wav2prg_false;
  }
  return wav2prg_true;
}

enum wav2prg_bool chr_recognize_itself(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct chr_private_state *state =(struct chr_private_state *)conf->private_state;

  return state->header[2] != 0;
  // state->header[3] == 0 means that the program starts with RUN
  // state->header[3] != 0 means that the program starts by jumping at the address stored at state->header[0] and state->header[1] (little-endian)
}

static enum wav2prg_bool chr_recognize_hc(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  if (block->info.start == 0x33c
   && block->info.end == 0x3fc
   && block->data[0] == 0x03
   && block->data[1] == 0xa7
   && block->data[2] == 0x02
   && block->data[3] == 0x04
   && block->data[4] == 0x03
   && block->data[0x352 - 0x33c] == 169
   && block->data[0x354 - 0x33c] == 141
   && block->data[0x355 - 0x33c] == 0x06
   && block->data[0x356 - 0x33c] == 0xdd
   && block->data[0x357 - 0x33c] == 0xa2){
    struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
 	  conf->thresholds[0] =
      block->data[0x358 - 0x33c] * 256 +
      block->data[0x353 - 0x33c];
    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers chr_observed_loaders[] = {
  {"Cauldron/Hewson/Rainbird",{"Cauldron/Hewson/Rainbird",  NULL, chr_recognize_itself}},
  {"Default C64", {"Cauldron/Hewson/Rainbird", NULL, chr_recognize_hc}},
  {NULL,{NULL,NULL,NULL}}
};

static const struct wav2prg_loaders chr[] =
{
  {
    "Cauldron/Hewson/Rainbird",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      chr_specialagent_get_block_info,
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
      chr_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      0x63,
      sizeof(chr_pilot_sequence),
      chr_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      &chr_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(chr,1,1,"Cauldron/Hewson/Rainbird loader", chr);
WAV2PRG_OBSERVER(chr, 1,0, chr_observed_loaders);
