/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * connection.c : Connection (AKA Chiocciola, Galadriel, Biturbo)
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool is_connection(struct wav2prg_observer_context *observer_context,
                                         const struct wav2prg_observer_functions *observer_functions,
                                         const struct program_block *datachunk_block,
                                         uint16_t start_point)
{
  uint16_t connection_thresholds[]={263};
  uint8_t connection_pilot_sequence[]={16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
  const struct wav2prg_plugin_conf connection_conf =
  {
   msbf,
   wav2prg_xor_checksum,
    wav2prg_compute_and_check_checksum,
    0,
    2,
    connection_thresholds,
    NULL,
    wav2prg_pilot_tone_with_shift_register,
    2,
    sizeof(connection_pilot_sequence),
    connection_pilot_sequence,
    0,
    first_to_last,
    wav2prg_false,
    NULL
  };
  struct wav2prg_plugin_conf *conf = observer_functions->use_different_conf_func(observer_context, &connection_conf);
  uint16_t start;

  if(datachunk_block->info.start != 698 || datachunk_block->info.end != 812)
    return wav2prg_false;
  
  if (datachunk_block->data[702-698] == 173 && datachunk_block->data[717-698] == 173)
    start=datachunk_block->data[791-698]*256+datachunk_block->data[773-698];
  else if (datachunk_block->data[702-698] == 165 && datachunk_block->data[717-698] == 165)
    start=2049;
  else
    return wav2prg_false;
    
  if (datachunk_block->data[707-698] == 173 && datachunk_block->data[712-698] == 173) {
    observer_functions->set_info_func(observer_context,
                                      start,
                                      datachunk_block->data[780-698]*256+datachunk_block->data[787-698],
                                      NULL);
    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers connection_dependency[] = {
  {"Kernal data chunk", {"Null loader", "Connection", is_connection}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(connection, 1,0, connection_dependency)
