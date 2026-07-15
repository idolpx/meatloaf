/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * racepsom.c : RAC Epsom (found on some Gremlin games, e.g. Switchblade and Lotus)
 */

#include "../wav2prg_api.h"

static uint16_t racepsom_thresholds[]={0x1b3,0x300};

enum wav2prg_sync_result racepsom_get_first_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint32_t pilots = 0;
  uint8_t pulse;

  do{
    if(functions->get_pulse_func(context, conf, &pulse) == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
    pilots++;
  }while(pulse == 2);
  return pilots >= conf->min_pilots ? wav2prg_sync_success : wav2prg_sync_failure;
}

enum wav2prg_bool racepsom_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t i;
  uint8_t unused_byte;
  uint16_t unused;
  uint16_t blocklen;

  if (functions->get_byte_func(context, functions, conf, &unused_byte) == wav2prg_false)
    return wav2prg_false;
  for(i = 0; i < 16; i++)
    if (functions->get_byte_func(context, functions, conf, (uint8_t*)info->name + i) == wav2prg_false)
      return wav2prg_false;

  if (functions->get_word_func(context, functions, conf, &unused) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &blocklen) == wav2prg_false)
    return wav2prg_false;
  if (!functions->get_sync(context, functions, conf, wav2prg_true))
    return wav2prg_false;
  info->end = info->start + blocklen;
  return wav2prg_true;
}

enum wav2prg_bool lotus_data_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  info->start = 0xa9dc;
  return racepsom_get_block_info(context, functions, conf, info);
}

static const struct wav2prg_loaders racepsom_functions[] =
{
  {
    "Lotus Data",
    {
      NULL,
      NULL,
      racepsom_get_first_sync,
      NULL,
      lotus_data_get_block_info,
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_add_checksum,
      wav2prg_do_not_compute_checksum,
      0,
      sizeof(racepsom_thresholds)/sizeof(*racepsom_thresholds) + 1,
      racepsom_thresholds,
      NULL,
      wav2prg_custom_pilot_tone,
      0,/*ignored, using wav2prg_custom_pilot_tone*/
      0,
      NULL,
      500,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {
    "RAC Epsom",
    {
      NULL,
      NULL,
      racepsom_get_first_sync,
      NULL,
      racepsom_get_block_info,
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_add_checksum,
      wav2prg_do_not_compute_checksum,
      0,
      sizeof(racepsom_thresholds)/sizeof(*racepsom_thresholds) + 1,
      racepsom_thresholds,
      NULL,
      wav2prg_custom_pilot_tone,
      0,/*ignored, using wav2prg_custom_pilot_tone*/
      0,
      NULL,
      500,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(racepsom, 1, 0, "RAC Epsom CHT", racepsom_functions)

static enum wav2prg_bool check_location(const uint8_t *data, uint8_t low_byte, enum wav2prg_bool also_check_low_byte, uint16_t *start)
{
  *start = data[1] + (data[3]<<8);
  return data[0] == 0xA2
       && data[2] == 0xA0
       && data[4] == 0x20
       && data[6] == 0x04
       && (!also_check_low_byte || data[5] == low_byte);
}

static enum wav2prg_bool recognize_racepsom(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point)
{
  uint16_t i, start;

  if(block->info.start != 0x400 || block->info.end < 1715 || block->info.end > 1760)
    return wav2prg_false;
  if(start_point == 0) {
    if(check_location(block->data + 18, 0, wav2prg_false, &start)) {
      observer_functions->set_restart_point_func(observer_context, 25);
      observer_functions->set_info_func(observer_context, start, 0, NULL);
      return wav2prg_true;
    }
    return wav2prg_false;
  }
  for(i = start_point; i + 6 < block->info.end - block->info.start; i++) {
     if(check_location(block->data + i, block->data[23], wav2prg_true, &start)){
      observer_functions->set_restart_point_func(observer_context, i + 7);
      observer_functions->set_info_func(observer_context, start, 0, NULL);
      return wav2prg_true;
    }
  }
  return wav2prg_false;
}

static const struct wav2prg_observers racepsom_observed_loaders[] = {
  {"Kernal data chunk", {"RAC Epsom", NULL, recognize_racepsom}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(racepsom, 1,0, racepsom_observed_loaders)
