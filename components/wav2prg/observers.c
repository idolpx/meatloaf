/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2011-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * observers.c : keep track of observers (from the data contained in a block,
 * observer try to guess the format the following block will be in)
 */
#include "observers.h"
#include "wav2prg_api.h"

#include <stdlib.h>
#include <string.h>

static struct wav2prg_observed {
  const char *name;
  struct obs_list *observers;
} *observed_list = NULL;

static struct obs_list** get_list_of_observers_maybe_adding_observed(const char *observed_name, enum wav2prg_bool add_if_missing){
  struct wav2prg_observed *current_observed;
  int number_of_observed = 0;

  if (observed_list == NULL)
    observed_list = calloc(sizeof(struct wav2prg_observed), 1);

  for(current_observed = observed_list; current_observed->name != NULL; current_observed++, number_of_observed++){
    if (!strcmp(observed_name, current_observed->name))
      return &current_observed->observers;
  }
  if (add_if_missing)
  {
    observed_list = realloc(observed_list, sizeof(observed_list[0]) * (2 + number_of_observed));
    observed_list[number_of_observed + 1].name = NULL;
    observed_list[number_of_observed + 1].observers = NULL;
    observed_list[number_of_observed].name = observed_name;
    observed_list[number_of_observed].observers = NULL;
    return &observed_list[number_of_observed].observers;
  }
  return NULL;
}

void add_observed(const char *observed_name, const struct wav2prg_observer_loaders *observer, void *module){
  struct obs_list **list = get_list_of_observers_maybe_adding_observed(observed_name, wav2prg_true);
  struct obs_list *new_element = malloc(sizeof(struct obs_list));

  new_element->observer = observer;
  new_element->module = module;
  /* recognition of Kernal loaders is added at end,
     recognition of anything else is added at beginning */
  if (!strcmp(observer->loader, "Default C64")
   || !strcmp(observer->loader, "Default C16")
   || !strcmp(observer->loader, "Kernal data chunk")
   || !strcmp(observer->loader, "Kernal data chunk C16")
   ){
    for(; *list; list = &(*list)->next);
  }
  new_element->next = *list;
  *list = new_element;
}

struct obs_list* get_observers(const char *observed_name){
  struct obs_list **observer = get_list_of_observers_maybe_adding_observed(observed_name, wav2prg_false);
  return observer ? *observer : NULL;
}

static void unregister_observer(struct obs_list **obs) {
  struct obs_list *new_next;

  if(*obs == NULL)
    return;

  new_next = (*obs)->next;
  free(*obs);
  *obs = new_next;
}

static void unregister_observers_from_module(void *module, struct obs_list **observers) {
  while (*observers){
    if (module == (*observers)->module)
      unregister_observer(observers);
    else
      observers = &(*observers)->next;
  }
}

void unregister_from_module_same_observed(void *module)
{
  int i = 0, number_of_observed;

  if (!observed_list)
    return;

  for(number_of_observed = 0; observed_list[number_of_observed].name != NULL; number_of_observed++);
  while(observed_list[i].name != NULL){
    unregister_observers_from_module(module, &observed_list[i].observers);

    if(observed_list[i].observers == NULL){
      memmove(observed_list + i, observed_list + i + 1, sizeof(observed_list[i]) * (number_of_observed - i));
      number_of_observed--;
    }
    else
      i++;
  }
}

void* get_module_of_first_observer(void)
{
  return observed_list && observed_list->observers ? observed_list->observers->module : NULL;
}
