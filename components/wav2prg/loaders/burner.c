/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * burner.c : Burner
 */

#include "../wav2prg_api.h"

struct burner_private_state {
  uint8_t block_termination;
  uint16_t entry_point;
};
struct burner_private_state burner_private_state_model={1};
static struct wav2prg_generate_private_state burner_generate_private_state = {
  sizeof(struct burner_private_state),
  &burner_private_state_model
};

static enum wav2prg_bool burner_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static enum wav2prg_bool burner1_get_block(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct wav2prg_raw_block* block, uint16_t block_size)
{
  struct burner_private_state *state =(struct burner_private_state *)conf->private_state;

  if (!functions->get_block_func(context, functions, conf, block, block_size))
    return wav2prg_false;
  state->block_termination--;
  return wav2prg_true;
}

static enum wav2prg_bool burner2_get_block(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct wav2prg_raw_block* block, uint16_t block_size)
{
  struct burner_private_state *state =(struct burner_private_state *)conf->private_state;

  if (!functions->get_block_func(context, functions, conf, block, block_size))
    return wav2prg_false;
  if (!functions->get_byte_func(context, functions, conf, &state->block_termination))
    return wav2prg_false;
  if (state->block_termination == 0){
    if (!functions->get_word_func(context, functions, conf, &state->entry_point))
      return wav2prg_false;
  }
  return wav2prg_true;
}

static enum wav2prg_bool is_burner2(struct wav2prg_observer_context *observer_context,
                                   const struct wav2prg_observer_functions *observer_functions,
                                   const struct program_block *headerchunk_entry,
                                   uint16_t start_point)
{
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct burner_private_state *state =(struct burner_private_state *)conf->private_state;
  uint16_t entry_point;

  if(headerchunk_entry->info.start != 828 || headerchunk_entry->info.end != 1020)
    return wav2prg_false;
  
  if(headerchunk_entry->data[1] == 0xa7
  && headerchunk_entry->data[2] == 0x02
  && headerchunk_entry->data[3] == 0x0c
  && headerchunk_entry->data[4] == 0x03
  && (headerchunk_entry->data[0x3c0 - 0x33c] == 0x7F || headerchunk_entry->data[0x3c0-0x33c] == 0x3F)
  && headerchunk_entry->data[0x3c0 - 0x33c] == headerchunk_entry->data[0x3d9-0x33c]
  && headerchunk_entry->data[0x3c5 - 0x33c] == headerchunk_entry->data[0x3cc-0x33c]
  && headerchunk_entry->data[0x3ab - 0x33c] == 0x79
  && headerchunk_entry->data[0x3ac - 0x33c] == 0x8d
  && headerchunk_entry->data[0x3ad - 0x33c] == 0x5a
  && headerchunk_entry->data[0x3ae - 0x33c] == 0x89
  && headerchunk_entry->data[0x3af - 0x33c] == 0xe9
  ){
    conf->endianness = (headerchunk_entry->data[0x3c0-0x33c] == 0x7F ? msbf : lsbf);
    conf->pilot_byte = headerchunk_entry->data[0x3c5-0x33c] ^ 0x59;
    conf->sync_sequence[0] = headerchunk_entry->data[0x3d0-0x33c] ^ 0x59;
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool is_burner1(struct wav2prg_observer_context *observer_context,
                                   const struct wav2prg_observer_functions *observer_functions,
                                   const struct program_block *headerchunk_entry,
                                   uint16_t start_point)
{
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct burner_private_state *state =(struct burner_private_state *)conf->private_state;
  uint16_t entry_point;

  if(headerchunk_entry->info.start != 828 || headerchunk_entry->info.end != 1020)
    return wav2prg_false;
  
  if(headerchunk_entry->data[1] == 0xa7
  && headerchunk_entry->data[2] == 0x02
  && headerchunk_entry->data[3] == 0x0c
  && headerchunk_entry->data[4] == 0x03
  && (headerchunk_entry->data[0x83] == 0x7F || headerchunk_entry->data[0x83] == 0x3F)
  && headerchunk_entry->data[0x88] == headerchunk_entry->data[0x8F]
  && headerchunk_entry->data[0x2F] == 0xD9
  && headerchunk_entry->data[0x34] == 0x58){
    conf->endianness = (headerchunk_entry->data[0x83] == 0x7F ? msbf : lsbf);
    conf->pilot_byte = headerchunk_entry->data[0x88] ^ 0x59;
    conf->sync_sequence[0] = headerchunk_entry->data[0x93] ^ 0x59;
    state->block_termination = headerchunk_entry->data[0x7F] ^ 0x59;
    state->entry_point = (headerchunk_entry->data[0x77] ^ 0x59) * 256 +
                         (headerchunk_entry->data[0x7a] ^ 0x59) + 1;
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool is_still_burner(struct wav2prg_observer_context *observer_context,
                                   const struct wav2prg_observer_functions *observer_functions,
                                   const struct program_block *headerchunk_entry,
                                   uint16_t start_point)
{
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);
  struct burner_private_state *state =(struct burner_private_state *)conf->private_state;
  return state->block_termination != 0;
}

static const struct wav2prg_observers burner_dependency[] = {
  {"Default C64"     , {"Burner type 1", NULL          , is_burner1}},
  {"Burner type 1"   , {"Burner type 1", "continuation", is_still_burner}},
  {"Default C64"     , {"Burner type 2", NULL          , is_burner2}},
  {"Burner type 2"   , {"Burner type 2", "continuation", is_still_burner}},
  {NULL, {NULL, NULL, NULL}}
};

uint16_t burner_thresholds[]={0x180};
uint8_t burner_sync_sequence[]={0xed};

static const struct wav2prg_loaders burner[] =
  {
    {
      "Burner type 1",
      {
        NULL,
        NULL,
        NULL,
        NULL,
        burner_get_block_info,
        burner1_get_block,
        NULL,
        NULL,
        NULL
      },
      {
        lsbf,
        wav2prg_xor_checksum,/*ignored */
        wav2prg_do_not_compute_checksum,
        0,
        2,
        burner_thresholds,
        NULL,
        wav2prg_pilot_tone_with_shift_register,
        0xe3,
        sizeof(burner_sync_sequence),
        burner_sync_sequence,
        10,
        first_to_last,
        wav2prg_false,
        &burner_generate_private_state
      }
    },
    {
      "Burner type 2",
      {
        NULL,
        NULL,
        NULL,
        NULL,
        burner_get_block_info,
        burner2_get_block,
        NULL,
        NULL,
        NULL
      },
      {
        lsbf,
        wav2prg_xor_checksum,/*ignored */
        wav2prg_do_not_compute_checksum,
        0,
        2,
        burner_thresholds,
        NULL,
        wav2prg_pilot_tone_with_shift_register,
        0xe3,
        sizeof(burner_sync_sequence),
        burner_sync_sequence,
        10,
        first_to_last,
        wav2prg_false,
        &burner_generate_private_state
      }
    },
    {NULL}
  };

WAV2PRG_LOADER(burner,1,0,"Burner", burner);
WAV2PRG_OBSERVER(burner, 1,0, burner_dependency)
