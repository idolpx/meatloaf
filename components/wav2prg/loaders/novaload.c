/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * novaload.c : Novaload
 */

#include "../wav2prg_api.h"

static uint16_t novaload_thresholds[]={500};
static uint8_t novaload_pilot_sequence[]={0xAA};

enum wav2prg_sync_result novaload_get_first_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint8_t shift_reg = 0xFF;
  uint8_t bit;
  uint8_t old_shift_reg_lsb;
  do{
    if(functions->get_bit_func(context,functions, conf, &bit) == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
    old_shift_reg_lsb = shift_reg & 1;
    shift_reg = (shift_reg >> 1) | (bit << 7);
  }while((!bit) || old_shift_reg_lsb);
  return functions->get_sync_sequence(context, functions, conf);
}

enum wav2prg_bool novaload_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t i;
  uint8_t namelen;
  uint16_t unused;
  uint16_t blocklen;

  if (functions->get_data_byte_func(context, functions, conf, &namelen, 0) == wav2prg_false)
    return wav2prg_false;
  /* Despite what Tapclean docs say, nothing forbids a Novaload program to have a name
     0x55 chars long. However, it is highly unlikely that the program name is very long.
     So, add a limit, to prevent false detections */
  if (namelen > 16)
    return wav2prg_false;
  for(i = 0; i < namelen; i++)
    if (functions->get_data_byte_func(context, functions, conf, (uint8_t*)info->name + i, 0) == wav2prg_false)
      return wav2prg_false;

  if (functions->get_data_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  /* According to Markus Brenner and Tomaz Kac, this should be the end address
     But the actual C64 implementation does not use these two bytes, so nor do we */
  if (functions->get_data_word_func(context, functions, conf, &unused) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_data_word_func(context, functions, conf, &blocklen) == wav2prg_false)
    return wav2prg_false;

  if (blocklen < 256 || info->start + blocklen > 65536)
    return wav2prg_false;
  info->end = info->start + blocklen;
  info->start+=256;
  return wav2prg_true;
}

static enum wav2prg_bool novaload_get_block(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct wav2prg_raw_block* block, uint16_t block_size)
{
  uint16_t bytes_received;
  uint16_t bytes_now;

  for(bytes_received = 0; bytes_received != block_size; bytes_received += bytes_now) {
    bytes_now = block_size - bytes_received > 256 ? 256 : block_size - bytes_received;
    if (functions->check_checksum_func(context, functions, conf) != wav2prg_checksum_state_correct)
      return wav2prg_false;
    if (functions->get_block_func(context, functions, conf, block, bytes_now) != wav2prg_true)
      return wav2prg_false;
  }
  return wav2prg_true;
}

static enum wav2prg_bool novaload_get_loaded_checksum(struct wav2prg_context *context, const struct wav2prg_functions *functions, struct wav2prg_plugin_conf *conf, uint8_t *byte)
{
  return functions->get_data_byte_func(context, functions, conf, byte, 0);
}

static const struct wav2prg_loaders novaload_functions[] =
{
  {
    "Novaload Normal",
    {
      NULL,
      NULL,
      novaload_get_first_sync,
      NULL,
      novaload_get_block_info,
      novaload_get_block,
      NULL,
      novaload_get_loaded_checksum,
      NULL
    },
    {
      lsbf,
      wav2prg_add_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      sizeof(novaload_thresholds)/sizeof(*novaload_thresholds) + 1,
      novaload_thresholds,
      NULL,
      wav2prg_custom_pilot_tone,
      0,/*ignored, using wav2prg_custom_pilot_tone*/
      sizeof(novaload_pilot_sequence),
      novaload_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(novaload, 1, 0, "Novaload (only normal blocks) loader", novaload_functions)
