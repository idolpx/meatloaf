/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2009-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * loaders.h : keeps a list of the supported loaders
 * (Meatloaf: static registration only)
 */
#ifndef WAV2PRG_LOADERS_H
#define WAV2PRG_LOADERS_H

#ifdef __cplusplus
extern "C" {
#endif

struct wav2prg_loaders;

void register_loaders(void);
const struct wav2prg_loaders* get_loader_by_name(const char*);

#ifdef __cplusplus
}
#endif

#endif /* WAV2PRG_LOADERS_H */
