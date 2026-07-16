/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Meatloaf addition (GPL, same license as WAV-PRG):
 *
 * turbotape_fast.c : Turbo Tape 64 variant with ~4x faster pulses
 * (bit 0 = ~88 cycles, bit 1 = ~144 cycles, threshold 116). The on-tape
 * layout is identical to Turbo Tape 64: pilot byte $02, countdown 9..1,
 * header block (type, start, end, byte, 16-char name), second sync, $00,
 * then data + XOR checksum.
 *
 * Detected from the CBM boot block: a $02A7-style IRQ loader stub that
 * waits for pilot byte $02 in zero page and samples bits from CIA1 ICR
 * (LDA $DC0D / ROL $02). Once one fast block decodes, the loader chains
 * to itself for the following blocks.
 */

#include "../wav2prg_api.h"

static uint16_t turbotape_fast_thresholds[]={116};
static uint8_t turbotape_fast_pilot_sequence[]={9,8,7,6,5,4,3,2,1};

static enum wav2prg_bool turbotape_fast_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct program_block_info* info)
{
  uint8_t byte;
  int i;

  if (functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
    return wav2prg_false;
  if (byte != 1 && byte != 2 && byte != 0x61)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
    return wav2prg_false;
  for(i=0;i<16;i++){
    if (functions->get_byte_func(context, functions, conf, (uint8_t*)info->name + i)  == wav2prg_false)
      return wav2prg_false;
  }
  if (functions->get_sync(context, functions, conf, wav2prg_false) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_byte_func(context, functions, conf, &byte) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

/* Recognize the fast-TT64 IRQ loader stub inside a CBM data block:
   - pilot wait   : LDA $02 / CMP #$02 / BNE  (A5 02 C9 02 D0)
   - bit sampling : LDA $DC0D            (AD 0D DC)
   - bit shifting : ROL $02              (26 02)                       */
static enum wav2prg_bool recognize_fast_stub(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point)
{
  uint16_t len = block->info.end - block->info.start;
  int found_wait = 0, found_cia = 0, found_rol = 0;
  uint16_t i;

  if (block->info.end <= block->info.start)
    return wav2prg_false;

  for (i = 0; i + 4 < len; i++)
  {
    if (block->data[i] == 0xA5 && block->data[i+1] == 0x02
     && block->data[i+2] == 0xC9 && block->data[i+3] == 0x02
     && block->data[i+4] == 0xD0)
      found_wait = 1;
    if (block->data[i] == 0xAD && block->data[i+1] == 0x0D && block->data[i+2] == 0xDC)
      found_cia = 1;
    if (block->data[i] == 0x26 && block->data[i+1] == 0x02)
      found_rol = 1;
  }

  return (found_wait && found_cia && found_rol) ? wav2prg_true : wav2prg_false;
}

/* After a fast block decodes successfully, keep using this loader for the
   next block (multi-part tapes); when its sync fails the engine falls
   back to the Kernal loader automatically */
static enum wav2prg_bool keep_using_loader(struct wav2prg_observer_context *observer_context,
                                           const struct wav2prg_observer_functions *observer_functions,
                                           const struct program_block *block,
                                           uint16_t start_point)
{
  return wav2prg_true;
}

static const struct wav2prg_observers turbotape_fast_observed_loaders[] = {
  {"Kernal data chunk",  {"Turbo Tape 64 fast", "Fast Turbo Tape stub", recognize_fast_stub}},
  {"Turbo Tape 64 fast", {"Turbo Tape 64 fast", "chain",                keep_using_loader}},
  {"Turbo Tape 64",      {"Turbo Tape 64",      "chain",                keep_using_loader}},
  {NULL, {NULL, NULL, NULL}}
};

static const struct wav2prg_loaders turbotape_fast_one_loader[] =
{
  {
    "Turbo Tape 64 fast",
    {
      NULL,
      NULL,
      NULL,
      NULL,
      turbotape_fast_get_block_info,
      NULL,
      NULL,
      NULL,
      NULL
    },
    {
      msbf,
      wav2prg_xor_checksum,
      wav2prg_compute_and_check_checksum,
      0,
      2,
      turbotape_fast_thresholds,
      NULL,
      wav2prg_pilot_tone_with_shift_register,
      2,
      sizeof(turbotape_fast_pilot_sequence),
      turbotape_fast_pilot_sequence,
      0,
      first_to_last,
      wav2prg_false,
      NULL
    }
  },
  {NULL}
};

WAV2PRG_LOADER(turbotape_fast,1,0,"Turbo Tape 64 fast (Meatloaf)", turbotape_fast_one_loader)
WAV2PRG_OBSERVER(turbotape_fast,1,0, turbotape_fast_observed_loaders)
