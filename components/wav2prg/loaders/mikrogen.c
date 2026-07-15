/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * mikrogen.c : found in many Mikro-Gen tapes (Automania etc.)
 */

#include "../wav2prg_api.h"

static uint16_t mikrogen_thresholds[]={544, 1441};

static enum wav2prg_sync_result mikrogen_get_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint32_t num_of_pilot_bits_found = -1;
  enum wav2prg_bool res;
  uint8_t pulse;

  do{
    res = functions->get_pulse_func(context, conf, &pulse);
    if (res == wav2prg_false)
      return wav2prg_wrong_pulse_when_syncing;
    num_of_pilot_bits_found++;
  }while(pulse == 2);
  return num_of_pilot_bits_found >= conf->min_pilots && pulse == 0
    ? wav2prg_sync_success
    : wav2prg_sync_failure;
}

static uint8_t mikrogen_compute_checksum_step(struct wav2prg_plugin_conf *conf, uint8_t old_checksum, uint8_t byte, uint16_t location_of_byte, uint32_t *extended_checksum)
{
  return old_checksum - byte;
}

static enum wav2prg_bool recognize_mikrogen_old(struct wav2prg_observer_context *observer_context,
                                         const struct wav2prg_observer_functions *observer_functions,
                                         const struct program_block *block,
                                         uint16_t start_point){
  uint16_t i, j;

  for (i = 0; i + 15 < block->info.end - block->info.start; i++)
    if (block->data[i     ] == 0xa9
     && block->data[i +  2] == 0x85
     && block->data[i +  3] == 0xfd
     && block->data[i +  4] == 0xa9
     && block->data[i +  6] == 0x85
     && block->data[i +  7] == 0xfe
     && block->data[i +  8] == 0xad
     && block->data[i +  9] == 0x0e
     && block->data[i + 10] == 0xdc
     && block->data[i + 11] == 0x29
     && block->data[i + 12] == 0xfe
     && block->data[i + 13] == 0x8d
     && block->data[i + 14] == 0x0e
     && block->data[i + 15] == 0xdc)
       break;
  if (i + 15 >= block->info.end - block->info.start)
    return wav2prg_false;
  for (j = i + 15; j + 6 < block->info.end - block->info.start; j++)
    if (block->data[j     ] == 0xe6
     && block->data[j +  1] == 0xfe
     && block->data[j +  2] == 0xa5
     && block->data[j +  3] == 0xfe
     && block->data[j +  4] == 0xc9
     && block->data[j +  6] == 0xd0)
       break;
  if (j + 6 == block->info.end - block->info.start)
    return wav2prg_false;
  observer_functions->set_info_func(observer_context,
                                    block->data[i +  5] * 256 + block->data[i +  1],
                                    block->data[j +  5] * 256 - 1,
                                    NULL);
  return wav2prg_true;
}

static enum wav2prg_bool recognize_mikrogen_new(struct wav2prg_observer_context *observer_context,
                                         const struct wav2prg_observer_functions *observer_functions,
                                         const struct program_block *block,
                                         uint16_t start_point){
  uint16_t i;

  for (i = start_point; i + 17 < block->info.end - block->info.start; i++){
    if (block->data[i + 0] == 0xa9
     && block->data[i + 2] == 0x85
     && block->data[i + 3] == 0xfd
     && block->data[i + 4] == 0xa9
     && block->data[i + 6] == 0x85
     && block->data[i + 7] == 0xfe
     && block->data[i + 8] == 0xa9
     && block->data[i + 10] == 0x85
     && block->data[i + 11] == 0xf8
     && block->data[i + 12] == 0xa9
     && block->data[i + 14] == 0x85
     && block->data[i + 15] == 0xf9
     && block->data[i + 16] == 0xad
     && block->data[i + 17] == 0x0e
      ){
      observer_functions->set_info_func(observer_context,
                                        block->data[i + 1] + block->data[i + 5] * 256,
                                        block->data[i + 9] + block->data[i + 13] * 256 - 1,
                                        NULL);
      observer_functions->set_restart_point_func(observer_context, i + 18);
      return wav2prg_true;
    }
  }
  return wav2prg_false;
}

static const struct wav2prg_observers mikrogen_observed_loaders[] = {
  {"Kernal data chunk", {"Mikro-Gen (old)", NULL, recognize_mikrogen_old}},
  {"Kernal data chunk", {"Mikro-Gen (new)", NULL, recognize_mikrogen_new}},
  {NULL, {NULL, NULL, NULL}}
};

static const struct wav2prg_loaders mikrogen_one_loader[] = 
{
  {
    "Mikro-Gen (old)",
    {
      NULL,
      NULL,
      mikrogen_get_sync,
      NULL,
      NULL,
      NULL,
      mikrogen_compute_checksum_step,
      NULL,
      NULL
    },
    {
      lsbf,
      wav2prg_xor_checksum /*ignored*/,
      wav2prg_compute_and_check_checksum,
      0,
      3,
      mikrogen_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_1_bits_followed_by_0,
      0x55,/*ignored*/
      0,
      NULL,
      256,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {
    "Mikro-Gen (new)",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      mikrogen_compute_checksum_step,
      NULL,
      NULL
    },
    {
      lsbf,
      wav2prg_xor_checksum /*ignored*/,
      wav2prg_compute_and_check_checksum,
      0,
      3,
      mikrogen_thresholds,
      NULL,
      wav2prg_pilot_tone_made_of_1_bits_followed_by_0,
      0x55,/*ignored*/
      0,
      NULL,
      256,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(mikrogen, 1, 0, "Mikro-Gen loader (old and new)", mikrogen_one_loader)
WAV2PRG_OBSERVER(mikrogen, 1,0, mikrogen_observed_loaders)
