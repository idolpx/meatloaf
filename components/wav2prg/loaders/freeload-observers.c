/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * freeload-observers.c : detects some variants of Freeload based on what precedes them
 */

#include "../wav2prg_api.h"

static enum wav2prg_bool recognize_fast_freeload(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x33c
   && block->info.end == 0x3fc
   && block->data[0] == 0x03
   && block->data[1] == 0xa7
   && block->data[2] == 0x02
   && block->data[3] == 0x04
   && block->data[4] == 0x03
   && block->data[0x3a7 - 0x33c] == 0xa9
   && block->data[0x3a9 - 0x33c] == 0x8d
   && block->data[0x3aa - 0x33c] == 0x05
   && block->data[0x3ab - 0x33c] == 0xdd
   && block->data[0x3ac - 0x33c] == 0xa9
   && block->data[0x3ae - 0x33c] == 0x8d
   && block->data[0x3af - 0x33c] == 0x04
   && block->data[0x3b0 - 0x33c] == 0xdd
  ){
    conf->thresholds[0] =
      block->data[0x3ad - 0x33c] + (block->data[0x3a8 - 0x33c] << 8);

    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_freeload_with_turbotape_sync(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x33c
   && block->info.end == 0x3fc){
    int i;
    for (i = 0; i + 19 < block->info.end - block->info.start; i++){
      if(block->data[i    ] == 0xa9
      && block->data[i + 2] == 0x8d
      && block->data[i + 3] == 0x06
      && block->data[i + 4] == 0xdd
      && block->data[i + 5] == 0x20
      && block->data[i + 7] == 0x03
      && block->data[i + 8] == 0x66
      && block->data[i + 9] == 0xbd
      && block->data[i + 10] == 0xa9
      && block->data[i + 12] == 0xc5
      && block->data[i + 13] == 0xbd
      && block->data[i + 14] == 0xd0
      && block->data[i + 15] == 0xf5
      && block->data[i + 16] == 0x85
      && block->data[i + 17] == 0x7b
      && block->data[i + 18] == 0xa0){
        uint8_t new_len = block->data[i + 19], j, sbyte;
        conf->thresholds[0] = block->data[i + 1];
        conf->pilot_byte =  block->data[i + 11];
        observer_functions->change_sync_sequence_length_func(conf, new_len);
        for(j = 0, sbyte = new_len; j < new_len; j++, sbyte--)
          conf->sync_sequence[j] = sbyte;
        conf->endianness = lsbf;
        conf->checksum_type = wav2prg_add_checksum;
        return wav2prg_true;
      }
    }
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_freeload_16(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 819
   && block->info.end   == 1010){
    if(block->data[0x3a1 - 819] == 0x20
    && block->data[0x3a2 - 819] == 0xC6
    && block->data[0x3a3 - 819] == 0x03
    && block->data[0x3a4 - 819] == 0x26
    && block->data[0x3a5 - 819] == 0xAC
    && block->data[0x3a6 - 819] == 0xA5
    && block->data[0x3a7 - 819] == 0xAC
    && block->data[0x3a8 - 819] == 0xC9
    && block->data[0x3a9 - 819] == block->data[0x3b0 - 819]
    && block->data[0x3aa - 819] == 0xD0
    && block->data[0x3ab - 819] == 0xF5
    && block->data[0x3ac - 819] == 0x20
    && block->data[0x3ad - 819] == 0xB8
    && block->data[0x3ae - 819] == 0x03
    && block->data[0x3af - 819] == 0xC9
    && block->data[0x3b1 - 819] == 0xF0
    && block->data[0x3b2 - 819] == 0xF9
    && block->data[0x3b3 - 819] == 0xC9
    && block->data[0x3b5 - 819] == 0xD0
    && block->data[0x3b6 - 819] == 0xEA){
      conf->pilot_byte =  block->data[0x3a9 - 819];
      conf->sync_sequence[0] = block->data[0x3b4 - 819];
      conf->thresholds[0] = (block->data[0x3d5 - 819] << 8) + block->data[0x3d7 - 819];
      conf->opposite_waveform = wav2prg_true;
      return wav2prg_true;
    }
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_firebird(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x33c
   && block->info.end == 0x3fc
   && block->data[0x1a] == 0xa2
   && block->data[0x1c] == 0x8e
   && block->data[0x1d] == 0x06
   && block->data[0x1e] == 0xdd
   && block->data[0x1f] == 0xa2
   && block->data[0x21] == 0x8e
   && block->data[0x22] == 0x07
   && block->data[0x23] == 0xdd
   && block->data[0x24] == 0xa2
   && block->data[0x25] == 0x19
   && block->data[0x26] == 0x8e
   && block->data[0x27] == 0x0f
   && block->data[0x28] == 0xdd
   && block->data[0x29] == 0xa0
   && block->data[0x2a] == 0x09
   && block->data[0x2b] == 0x20
   && block->data[0x2d] == 0x03
   && block->data[0x2e] == 0xc9
   && block->data[0x2f] == 0x02
   && block->data[0x30] == 0xd0
   && block->data[0x31] == 0xf7
   && block->data[0x32] == 0x20
   && block->data[0x34] == 0x03
   && block->data[0x35] == 0xc9
   && block->data[0x36] == 0x02
   && block->data[0x37] == 0xf0
   && block->data[0x38] == 0xf9
   && block->data[0x39] == 0xc9
   && block->data[0x3b] == 0xd0
   && block->data[0x3c] == 0xec
   && block->data[0x3d] == 0x20
   && block->data[0x3f] == 0x03
   && block->data[0x40] == 0xc9
   && block->data[0x42] == 0xd0
   && block->data[0x43] == 0xe5
  ){
    conf->thresholds[0] =
      block->data[0x1b] + (block->data[0x20] << 8);
    conf->pilot_byte = 2;
    observer_functions->change_sync_sequence_length_func(conf, 2);
    conf->sync_sequence[0] = block->data[0x3a];
    conf->sync_sequence[1] = block->data[0x41];
    conf->endianness = lsbf;

    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_algasoft(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x33c
   && block->info.end == 0x3fc
   && block->data[0x38] == 0xa9
   && block->data[0x3a] == 0x8d
   && block->data[0x3b] == 0x06
   && block->data[0x3c] == 0xdd
   && block->data[0x3d] == 0xa9
   && block->data[0x3f] == 0x8d
   && block->data[0x40] == 0x07
   && block->data[0x41] == 0xdd
   && block->data[0x42] == 0x8e
   && block->data[0x43] == 0x0f
   && block->data[0x44] == 0xdd
   && block->data[0x45] == 0xa0
   && block->data[0x46] == 0x09
   && block->data[0x47] == 0x20
   && block->data[0x48] == 0x53
   && block->data[0x49] == 0x03
   && block->data[0x4a] == 0xc9
   && block->data[0x4b] == 0x02
   && block->data[0x4c] == 0xd0
   && block->data[0x4d] == 0xf7
   && block->data[0x4e] == 0x20
   && block->data[0x4f] == 0x51
   && block->data[0x50] == 0x03
   && block->data[0x51] == 0xc9
   && block->data[0x52] == 0x02
   && block->data[0x53] == 0xf0
   && block->data[0x54] == 0xf9
   && block->data[0x55] == 0xc9
   && block->data[0x57] == 0xd0
   && block->data[0x58] == 0xec
   && block->data[0x59] == 0x20
   && block->data[0x5a] == 0x51
   && block->data[0x5b] == 0x03
   && block->data[0x5c] == 0xc9
   && block->data[0x5e] == 0xd0
   && block->data[0x5f] == 0xe5){
    conf->thresholds[0] = block->data[0x39] + 256 * block->data[0x3e];
    conf->pilot_byte = 2;
    observer_functions->change_sync_sequence_length_func(conf, 2);
    conf->sync_sequence[0] = block->data[0x56];
    conf->sync_sequence[1] = block->data[0x5d];
    conf->endianness = lsbf;
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_catalypse(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x800
   && block->info.end == 0xc038
   && block->data[0xb419 - 0x800] == 0xa9
   && block->data[0xb41b - 0x800] == 0x8d
   && block->data[0xb41c - 0x800] == 0x06
   && block->data[0xb41d - 0x800] == 0xdd
   && block->data[0xb41e - 0x800] == 0xa9
   && block->data[0xb420 - 0x800] == 0x8d
   && block->data[0xb421 - 0x800] == 0x07
   && block->data[0xb422 - 0x800] == 0xdd
   && block->data[0xb423 - 0x800] == 0x8e
   && block->data[0xb424 - 0x800] == 0x0f
   && block->data[0xb425 - 0x800] == 0xdd
   && block->data[0xb426 - 0x800] == 0xa0
   && block->data[0xb427 - 0x800] == 0x09
   && block->data[0xb428 - 0x800] == 0x20
   && block->data[0xb429 - 0x800] == 0xb8
   && block->data[0xb42a - 0x800] == 0x04
   && block->data[0xb42b - 0x800] == 0xc9
   && block->data[0xb42c - 0x800] == 0x02
   && block->data[0xb42d - 0x800] == 0xd0
   && block->data[0xb42e - 0x800] == 0xf7
   && block->data[0xb42f - 0x800] == 0x20
   && block->data[0xb430 - 0x800] == 0x95
   && block->data[0xb431 - 0x800] == 0x04
   && block->data[0xb432 - 0x800] == 0xc9
   && block->data[0xb433 - 0x800] == 0x02
   && block->data[0xb434 - 0x800] == 0xf0
   && block->data[0xb435 - 0x800] == 0xf9
   && block->data[0xb436 - 0x800] == 0xc9
   && block->data[0xb438 - 0x800] == 0xd0
   && block->data[0xb439 - 0x800] == 0xec
   && block->data[0xb43a - 0x800] == 0x20
   && block->data[0xb43b - 0x800] == 0x95
   && block->data[0xb43c - 0x800] == 0x04
   && block->data[0xb43d - 0x800] == 0xc9
   && block->data[0xb43f - 0x800] == 0xd0
   && block->data[0xb440 - 0x800] == 0xe5){
    conf->thresholds[0] = block->data[0xb41a-0x800] + 256 * block->data[0xb41f-0x800];
    conf->pilot_byte = 2;
    observer_functions->change_sync_sequence_length_func(conf, 2);
    conf->sync_sequence[0] = block->data[0xb437 - 0x800];
    conf->sync_sequence[1] = block->data[0xb43e - 0x800];
    conf->endianness = lsbf;
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_ode(struct wav2prg_observer_context *observer_context,
                                             const struct wav2prg_observer_functions *observer_functions,
                                             const struct program_block *block,
                                             uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x33c
   && block->info.end == 0x3fc
   && block->data[0] == 0x03
   && block->data[1] == 0xab
   && block->data[2] == 0x02
   && block->data[3] == 0x04
   && block->data[4] == 0x03
   && block->data[0x368 - 0x33c] == 0xc9
   && block->data[0x37c - 0x33c] == 0xc9
   && block->data[0x369 - 0x33c] == block->data[0x37d - 0x33c]
   && block->data[0x380 - 0x33c] == 0xc9
  ){
    conf->thresholds[0] = 0x1b0;
    conf->pilot_byte = block->data[0x369 - 0x33c];
    conf->sync_sequence[0] = block->data[0x381 - 0x33c];
    return wav2prg_true;
  }
  return wav2prg_false;
}

static enum wav2prg_bool recognize_genuine(struct wav2prg_observer_context *observer_context,
                                           const struct wav2prg_observer_functions *observer_functions,
                                           const struct program_block *block,
                                           uint16_t start_point){
  struct wav2prg_plugin_conf *conf = observer_functions->get_conf_func(observer_context);

  if (block->info.start == 0x2a7
   && block->info.end == 0x334
   && block->data[0x2ac -0x2a7] == 0xA9
   && block->data[0x2ad -0x2a7] == 0x1F
   && block->data[0x2ae -0x2a7] == 0x8D
   && block->data[0x2af -0x2a7] == 0x0D
   && block->data[0x2b0 -0x2a7] == 0xDC
   && block->data[0x2b1 -0x2a7] == 0xAD
   && block->data[0x2b2 -0x2a7] == 0x0D
   && block->data[0x2b3 -0x2a7] == 0xDC
   && block->data[0x2b4 -0x2a7] == 0xA9
   && block->data[0x2b6 -0x2a7] == 0x8D
   && block->data[0x2b7 -0x2a7] == 0x04
   && block->data[0x2b8 -0x2a7] == 0xDC
   && block->data[0x2b9 -0x2a7] == 0xA9
   && block->data[0x2bb -0x2a7] == 0x8D
   && block->data[0x2bc -0x2a7] == 0x05
   && block->data[0x2bd -0x2a7] == 0xDC
   && block->data[0x2be -0x2a7] == 0xA9
   && block->data[0x2bf -0x2a7] == 0x90
   && block->data[0x2c0 -0x2a7] == 0x8D
   && block->data[0x2c1 -0x2a7] == 0x0D
   && block->data[0x2c2 -0x2a7] == 0xDC
   && block->data[0x2c3 -0x2a7] == 0xA9
   && block->data[0x2c4 -0x2a7] == 0x51
   && block->data[0x2c5 -0x2a7] == 0x8D
   && block->data[0x2c6 -0x2a7] == 0xFE
   && block->data[0x2c7 -0x2a7] == 0xFF
   && block->data[0x2c8 -0x2a7] == 0xA9
   && block->data[0x2c9 -0x2a7] == 0x03
   && block->data[0x2ca -0x2a7] == 0x8D
   && block->data[0x2cb -0x2a7] == 0xFF
   && block->data[0x2cc -0x2a7] == 0xFF
  ){
    conf->thresholds[0] = (block->data[0x2ba - 0x2a7] << 8) + block->data[0x2b5 - 0x2a7];
    if (conf->thresholds[0] > 0x600)
      conf->thresholds[0] -= 0x600;
    else
      conf->thresholds[0] -= 0x200;
    return wav2prg_true;
  }
  return wav2prg_false;
}

static const struct wav2prg_observers freeload_observers[] = {
  {"Default C64", {"Freeload", "fast", recognize_fast_freeload}},
  {"Default C64", {"Freeload", "Turbo Tape 64-like sync", recognize_freeload_with_turbotape_sync}},
  {"Default C16", {"Freeload", "C16", recognize_freeload_16}},
  {"Default C64", {"Freeload", "Firebird", recognize_firebird}},
  {"Default C64", {"Freeload", "Algasoft/Magnifici 7", recognize_algasoft}},
  {"Null loader", {"Freeload", "Catalypse", recognize_catalypse}},
  {"Default C64", {"Freeload", "odeLOAD", recognize_ode}},
  {"Kernal data chunk", {"Freeload", "genuine", recognize_genuine}},
  {NULL, {NULL, NULL, NULL}}
};

WAV2PRG_OBSERVER(freeload_observers, 1,0, freeload_observers)
