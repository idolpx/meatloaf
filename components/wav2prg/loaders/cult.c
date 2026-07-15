/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * cult.c : Cult format (found on some Zzap! Megatapes and some games)
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool is_cult(struct wav2prg_observer_context *observer_context,
                                         const struct wav2prg_observer_functions *observer_functions,
                                         const struct program_block *headerchunk_entry,
                                         uint16_t start_point)
{
  uint16_t cult_thresholds[]={0x1a0};
  uint8_t cult_pilot_sequence[]={0xaa};
  const struct wav2prg_plugin_conf cult_conf =
  {
    lsbf,
    wav2prg_xor_checksum,/*ignored*/
    wav2prg_do_not_compute_checksum,
    0,
    2,
    cult_thresholds,
    NULL,
    wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
    2,/*ignored*/
    sizeof(cult_pilot_sequence),
    cult_pilot_sequence,
    2000,
    first_to_last,
    wav2prg_false,
    NULL
  };
  struct wav2prg_plugin_conf *conf = observer_functions->use_different_conf_func(observer_context, &cult_conf);
  uint16_t start;

  if(headerchunk_entry->info.start != 828 || headerchunk_entry->info.end != 1020)
    return wav2prg_false;
  
  if (headerchunk_entry->data[1] == 0xe0
   && headerchunk_entry->data[2] == 0x02
   && headerchunk_entry->data[3] == 0x04
   && headerchunk_entry->data[4] == 0x03
   && headerchunk_entry->data[0x358 - 0x33c] == 0x85
   && headerchunk_entry->data[0x359 - 0x33c] == 0xae){
    observer_functions->set_info_func(observer_context,
                                      2049,
                                      headerchunk_entry->data[0x357 - 0x33c] +
                                      256 * headerchunk_entry->data[0x35b - 0x33c],
                                      NULL);
    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers cult_dependency[] = {
  {"Default C64", {"Null loader", "Cult", is_cult}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(cult, 1,0, cult_dependency)
