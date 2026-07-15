/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2011-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * observers.h : keep track of observers (from the data contained in a block,
 * observer try to guess the format the following block will be in)
 */
#ifndef WAV2PRG_OBSERVERS_H
#define WAV2PRG_OBSERVERS_H

struct wav2prg_observer_loaders;
struct obs_list {
  const struct wav2prg_observer_loaders *observer;
  void *module;
  struct obs_list *next;
};

void add_observed(const char *observer_name, const struct wav2prg_observer_loaders *observed, void *module);
struct obs_list* get_observers(const char *observed_name);
void unregister_from_module_same_observed(void *module);
void* get_module_of_first_observer(void);

#endif /* WAV2PRG_OBSERVERS_H */
