/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2009-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * loaders.c : keeps a list of the supported loaders
 * (Meatloaf: all loader plugins are statically linked and registered here)
 */
#include "wav2prg_api.h"

#include <string.h>
#include <stdlib.h>

#include "loaders.h"
#include "observers.h"

static struct loader {
  const struct wav2prg_loaders *loader;
  struct loader *next;
} *loader_list = NULL;

static const struct loader* get_a_loader(const char* name) {
  struct loader *loader;

  for(loader = loader_list; loader != NULL; loader = loader->next)
  {
    if(!strcmp(loader->loader->name, name))
      return loader;
  }
  return NULL;
}

const struct wav2prg_loaders* get_loader_by_name(const char* name) {
  const struct loader *loader = get_a_loader(name);
  return loader ? loader->loader : NULL;
}

static void register_loader(const struct wav2prg_all_loaders *loader) {
  struct loader **last_loader;
  const struct wav2prg_loaders *one_loader;
  char api_version[4] = WAVPRG_LOADER_API;

  if (loader->api_version[0] != api_version[0]
   || loader->api_version[1] != api_version[1]
   || loader->api_version[2] != api_version[2]
   || loader->api_version[3] != api_version[3])
    return;

  for(one_loader = loader->loaders; one_loader->name; one_loader++)
  {
    if (get_loader_by_name(one_loader->name))
      continue;/*duplicate name*/

    for(last_loader = &loader_list; *last_loader != NULL; last_loader = &(*last_loader)->next);
    *last_loader = (struct loader *)malloc(sizeof(struct loader));
    (*last_loader)->loader = one_loader;
    (*last_loader)->next = NULL;
  }
}

static void register_observer(const struct wav2prg_all_observers *observer) {
  const struct wav2prg_observers *one_observer;
  char api_version[4] = WAVPRG_OBSERVER_API;

  if (observer->api_version[0] != api_version[0]
   || observer->api_version[1] != api_version[1]
   || observer->api_version[2] != api_version[2]
   || observer->api_version[3] != api_version[3])
    return;

  for(one_observer = observer->observers; one_observer->observed_name; one_observer++)
    add_observed(one_observer->observed_name, &one_observer->observers, NULL);
}

#define STATIC_LOADER(x) \
{ \
  extern const struct wav2prg_all_loaders wav2prg_loader_##x; \
  register_loader(&wav2prg_loader_##x); \
}
#define STATIC_OBSERVER(x) \
{ \
  extern const struct wav2prg_all_observers wav2prg_observer_##x; \
  register_observer(&wav2prg_observer_##x); \
}

void register_loaders(void) {
  static int registered = 0;
  if (registered)
    return;
  registered = 1;

  /* Loader plugins */
  STATIC_LOADER(kernal)
  STATIC_LOADER(turbotape)
  STATIC_LOADER(turbotape_fast)
  STATIC_LOADER(novaload)
  STATIC_LOADER(novaload_special)
  STATIC_LOADER(audiogenic)
  STATIC_LOADER(pavlodapenetrator)
  STATIC_LOADER(pavlodaold)
  STATIC_LOADER(pavloda)
  STATIC_LOADER(rackit)
  STATIC_LOADER(turbo220)
  STATIC_LOADER(freeload)
  STATIC_LOADER(wildsave)
  STATIC_LOADER(theedge)
  STATIC_LOADER(maddoctor)
  STATIC_LOADER(mikrogen)
  STATIC_LOADER(crl)
  STATIC_LOADER(snakeload)
  STATIC_LOADER(snake)
  STATIC_LOADER(nobby)
  STATIC_LOADER(microload)
  STATIC_LOADER(atlantis)
  STATIC_LOADER(wizarddev)
  STATIC_LOADER(jetload)
  STATIC_LOADER(alien)
  STATIC_LOADER(anirog)
  STATIC_LOADER(ashdave)
  STATIC_LOADER(binarydesign)
  STATIC_LOADER(burner)
  STATIC_LOADER(chr)
  STATIC_LOADER(empire)
  STATIC_LOADER(flashload)
  STATIC_LOADER(flimbo)
  STATIC_LOADER(jedi)
  STATIC_LOADER(ocean)
  STATIC_LOADER(racepsom)
  STATIC_LOADER(samuraitrilogy)
  STATIC_LOADER(tequilasunrise)
  STATIC_LOADER(thunderload)
  STATIC_LOADER(usgoldfirst)
  STATIC_LOADER(usgoldlast)
  STATIC_LOADER(visiload)

  /* Observers (loader detection from loaded blocks) */
  STATIC_OBSERVER(kernal)
  STATIC_OBSERVER(turbotape)
  STATIC_OBSERVER(turbotape_fast)
  STATIC_OBSERVER(novaload_observers)
  STATIC_OBSERVER(novaload_special)
  STATIC_OBSERVER(audiogenic)
  STATIC_OBSERVER(rackit)
  STATIC_OBSERVER(freeload_observers)
  STATIC_OBSERVER(wildsave)
  STATIC_OBSERVER(maddoctor)
  STATIC_OBSERVER(mikrogen)
  STATIC_OBSERVER(crl)
  STATIC_OBSERVER(snakeload)
  STATIC_OBSERVER(snake)
  STATIC_OBSERVER(anirog)
  STATIC_OBSERVER(binarydesign)
  STATIC_OBSERVER(burner)
  STATIC_OBSERVER(chr)
  STATIC_OBSERVER(connection)
  STATIC_OBSERVER(cult)
  STATIC_OBSERVER(dragonload)
  STATIC_OBSERVER(empire)
  STATIC_OBSERVER(flashload)
  STATIC_OBSERVER(jedi)
  STATIC_OBSERVER(ocean)
  STATIC_OBSERVER(opera)
  STATIC_OBSERVER(racepsom)
  STATIC_OBSERVER(samuraitrilogy)
  STATIC_OBSERVER(thunderload)
  STATIC_OBSERVER(visiload)
  STATIC_OBSERVER(yleisur)
}
