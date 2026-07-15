/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2009-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * wav2prg_core.h : main processing file to detect programs from pulses
 */
#ifndef WAV2PRG_CORE_H
#define WAV2PRG_CORE_H


#include "wavprg_types.h"

#include <stdint.h>

struct wav2prg_display_interface;
struct display_interface_internal;
struct wav2prg_plugin_conf;
struct wav2prg_input_object;
struct wav2prg_input_functions;

typedef enum wav2prg_bool (*wav2prg_get_rawpulse_func)(void* audiotap, uint32_t* rawpulse);
typedef enum wav2prg_bool (*wav2prg_test_eof_func)(void* audiotap);
typedef int32_t           (*wav2prg_get_pos_func)(void* audiotap);

#ifdef __cplusplus
extern "C" {
#endif

struct block_list_element* wav2prg_analyse(const char* start_loader,
                                           struct wav2prg_plugin_conf* start_conf,
                                           enum wav2prg_bool keep_broken_blocks,
                                           enum wav2prg_bool stop_at_end_of_program,
                                           struct wav2prg_input_object *input_object,
                                           struct wav2prg_input_functions *input,
                                           struct wav2prg_display_interface *wav2prg_display_interface,
                                           struct display_interface_internal *display_interface_internal);


#ifdef __cplusplus
}
#endif

#endif /* WAV2PRG_CORE_H */
