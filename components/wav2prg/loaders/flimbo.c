/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * flimbo.c : Flimbo's Quest
 */

#include "../wav2prg_api.h"

static uint16_t flimbo_thresholds[]={0x1a0};
static uint8_t flimbo_pilot_sequence[]={0x42,0x4c};
struct flimbo_private_state {
  uint8_t fileid;
};
static struct wav2prg_generate_private_state flimbo_generate_private_state = {
  sizeof(struct flimbo_private_state),
  NULL
};

static enum wav2prg_bool flimbo_get_bit(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* bit)
{
  if (!functions->get_bit_func(context, functions, conf, bit))
    return wav2prg_false;
  *bit = *bit ? 0 : 1;
  return wav2prg_true;
}

static enum wav2prg_sync_result flimbo_first_get_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint8_t byte;
  uint32_t i;
  uint8_t bit;
  enum wav2prg_sync_result res;
  
  res = functions->get_sync_sequence(context, functions, conf);
  if (res != wav2prg_sync_success)
    return res;
  if (flimbo_get_bit(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 0)
    return wav2prg_sync_failure;
  if (flimbo_get_bit(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 0)
    return wav2prg_sync_failure;
  if (flimbo_get_bit(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 0)
    return wav2prg_sync_failure;
  if (flimbo_get_bit(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 0)
    return wav2prg_sync_failure;
  if (flimbo_get_bit(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 1)
    return wav2prg_sync_failure;
  if (flimbo_get_bit(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 0)
    return wav2prg_sync_failure;
  if (flimbo_get_bit(context, functions, conf, &bit) == wav2prg_false)
    return wav2prg_wrong_pulse_when_syncing;
  if (bit != 1)
    return wav2prg_sync_failure;
  return wav2prg_sync_success;
}

static enum wav2prg_bool flimbo_first_get_block_info(struct wav2prg_context *context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info *info)
{
  uint16_t entry_point;
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &entry_point) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static enum wav2prg_bool flimbo_following_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t blockid;
  struct flimbo_private_state *state = (struct flimbo_private_state*)conf->private_state;

  functions->reset_checksum_to_func(context, 0x4c ^ 0x42);
  if (functions->get_data_byte_func(context, functions, conf, &state->fileid, 0) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_data_byte_func(context, functions, conf, &blockid, 0) == wav2prg_false || blockid != 0)
    return wav2prg_false;
  if (functions->check_checksum_func(context, functions, conf) != wav2prg_checksum_state_correct)
    return wav2prg_false;
  functions->number_to_name_func(state->fileid, info->name);
  if (functions->get_data_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_data_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static enum wav2prg_bool flimbo_following_get_block(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct wav2prg_raw_block* block, uint16_t block_size)
{
  uint16_t bytes_received;
  uint16_t bytes_now;
  uint8_t expected_blockid = 1, fileid, blockid;
  struct flimbo_private_state *state = (struct flimbo_private_state*)conf->private_state;

  for(bytes_received = 0; bytes_received != block_size; bytes_received += bytes_now) {
    bytes_now = block_size - bytes_received > 256 ? 256 : block_size - bytes_received;
    if (functions->check_checksum_func(context, functions, conf) != wav2prg_checksum_state_correct)
      return wav2prg_false;
    if (!functions->get_sync(context, functions, conf, wav2prg_false))
      return wav2prg_false;
    functions->reset_checksum_to_func(context, 0x4c ^ 0x42);
    if (functions->get_data_byte_func(context, functions, conf, &fileid, 0) == wav2prg_false || fileid != state->fileid)
      return wav2prg_false;
    if (functions->get_data_byte_func(context, functions, conf, &blockid, 0) == wav2prg_false || blockid != expected_blockid++)
      return wav2prg_false;
    if (functions->check_checksum_func(context, functions, conf) != wav2prg_checksum_state_correct)
      return wav2prg_false;
    if (functions->get_block_func(context, functions, conf, block, bytes_now) != wav2prg_true)
      return wav2prg_false;
  }
  return wav2prg_true;
}

static const struct wav2prg_loaders flimbo_functions[] = {
  {
    "Flimbo's Quest first",
    {
      flimbo_get_bit,
      NULL,
      flimbo_first_get_sync,
      NULL,
      flimbo_first_get_block_info,
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
      flimbo_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
      0x55,
      0,/*ignored*/
      NULL,/*ignored*/
      8000,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {
    "Flimbo's Quest following",
    {
      flimbo_get_bit,
      NULL,
      NULL,
      NULL,
      flimbo_following_get_block_info,
      flimbo_following_get_block,
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
      flimbo_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      0x85,
      2,
      flimbo_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      &flimbo_generate_private_state
    }
  },
  {NULL}
};

WAV2PRG_LOADER(flimbo, 1, 0, "Flimbo's Quest loaders, after SLC's description", flimbo_functions)
