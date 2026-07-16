/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Meatloaf addition (GPL, same license as WAV-PRG):
 *
 * novaload_observers.c : auto-detection for the Novaload loader.
 *
 * Upstream wav-prg has the "Novaload Normal" plugin but no observer for
 * it - on the desktop the user selects Novaload manually as the start
 * loader. For automatic sequential decoding, recognize the Novaload boot
 * program inside a CBM data block: it contains the loader name "NOVALOAD"
 * as C64 screen codes (0E 0F 16 01 0C 0F 01 04). Once a Novaload block
 * decodes, keep using the loader for the following blocks.
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool recognize_novaload_boot(struct wav2prg_observer_context *observer_context,
                                                 const struct wav2prg_observer_functions *observer_functions,
                                                 const struct program_block *block,
                                                 uint16_t start_point)
{
  /* "NOVALOAD" in screen codes */
  static const unsigned char sig[8] = {0x0E, 0x0F, 0x16, 0x01, 0x0C, 0x0F, 0x01, 0x04};
  uint16_t len = block->info.end - block->info.start;
  uint16_t i, j;

  if (block->info.end <= block->info.start)
    return wav2prg_false;

  for (i = 0; i + sizeof(sig) <= len; i++)
  {
    for (j = 0; j < sizeof(sig); j++)
      if (block->data[i + j] != sig[j])
        break;
    if (j == sizeof(sig))
      return wav2prg_true;
  }
  return wav2prg_false;
}

/* After a Novaload block decodes successfully, keep using the loader for
   the next block; when its sync fails the engine falls back to the Kernal
   loader automatically */
static enum wav2prg_bool keep_using_novaload(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point)
{
  return wav2prg_true;
}

static const struct wav2prg_observers novaload_observed_loaders[] = {
  {"Kernal data chunk", {"Novaload Normal", "Novaload boot program", recognize_novaload_boot}},
  {"Novaload Normal",   {"Novaload Normal", "chain",                 keep_using_novaload}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(novaload_observers,1,0, novaload_observed_loaders)
