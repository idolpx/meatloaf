/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2010-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * get_pulse.h : read duration of a pulse from a tape, and convert to one of
 * the possible pulses in the current format. The possible pulses are numbered
 * from 0 (the shortest)
 */
#ifndef WAV2PRG_GET_PULSE_H
#define WAV2PRG_GET_PULSE_H

struct tolerances;

enum wav2prg_bool get_pulse_adaptively_tolerant(uint32_t raw_pulse, uint8_t num_pulse_lengths, struct tolerances *tolerances, uint8_t* pulse);

enum wav2prg_bool get_pulse_in_measured_ranges(uint32_t raw_pulse, const struct tolerances *tolerances, uint8_t num_pulse_lengths, uint8_t* pulse);

struct tolerances* get_tolerances(uint8_t, const uint16_t*);
const struct tolerances* get_existing_tolerances(uint8_t num_pulse_lengths, const uint16_t *thresholds);
void add_or_replace_tolerances(uint8_t, const uint16_t*, struct tolerances*);
void copy_tolerances(uint8_t num_pulse_lengths, struct tolerances *dest, const struct tolerances *src);
struct tolerances* new_copy_tolerances(uint8_t num_pulse_lengths, const struct tolerances *src);

uint16_t get_average(const struct tolerances*, uint8_t);
uint16_t get_min_measured(const struct tolerances *tolerance, uint8_t pulse);
uint16_t get_max_measured(const struct tolerances *tolerance, uint8_t pulse);

void set_pulse_retrieval_mode(uint32_t new_distance, enum wav2prg_bool use_distance_from_average);
uint32_t get_pulse_retrieval_mode(enum wav2prg_bool *use_distance_from_average);

void reset_tolerances(void);

#endif /* WAV2PRG_GET_PULSE_H */
