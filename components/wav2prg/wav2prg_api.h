/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 2009-2012
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * wav2prg_api.h : keep track of observers (from the data contained in a block,
 * observer try to guess the format the following block will be in)
 */
#ifndef WAV2PRG_API_H
#define WAV2PRG_API_H

#include "wavprg_types.h"
#include "program_block.h"
#include "checksum_state.h"

#include <stdint.h>
#include <stddef.h>

enum wav2prg_plugin_endianness {
  lsbf,
  msbf
};

enum wav2prg_checksum {
  wav2prg_xor_checksum,
  wav2prg_add_checksum
};

enum wav2prg_checksum_computation {
  wav2prg_do_not_compute_checksum,
  wav2prg_compute_checksum_but_do_not_check_it_at_end,
  wav2prg_compute_and_check_checksum
};

enum wav2prg_findpilot_type {
  wav2prg_pilot_tone_with_shift_register,
  wav2prg_pilot_tone_made_of_1_bits_followed_by_0,
  wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
  wav2prg_custom_pilot_tone
};

enum wav2prg_block_filling {
  first_to_last,
  last_to_first
};

enum wav2prg_sync_result {
  wav2prg_sync_success,
  wav2prg_sync_failure,
  wav2prg_wrong_pulse_when_syncing
};

struct wav2prg_raw_block;
struct wav2prg_context;
struct wav2prg_functions;
struct wav2prg_plugin_conf;
struct wav2prg_plugin_functions;

typedef enum wav2prg_bool (*wav2prg_get_pulse_func)(struct wav2prg_context*, struct wav2prg_plugin_conf*, uint8_t*);
typedef enum wav2prg_bool (*wav2prg_get_bit_func)(struct wav2prg_context*, const struct wav2prg_functions*, struct wav2prg_plugin_conf*, uint8_t*);
typedef enum wav2prg_bool (*wav2prg_get_byte_func)(struct wav2prg_context*, const struct wav2prg_functions*, struct wav2prg_plugin_conf*, uint8_t*);
typedef enum wav2prg_bool (*wav2prg_get_data_byte_func)(struct wav2prg_context*, const struct wav2prg_functions*, struct wav2prg_plugin_conf*, uint8_t*, uint16_t);
typedef enum wav2prg_bool (*wav2prg_get_word_func)(struct wav2prg_context*, const struct wav2prg_functions*, struct wav2prg_plugin_conf*, uint16_t*);
typedef enum wav2prg_bool (*wav2prg_get_block_func)(struct wav2prg_context*, const struct wav2prg_functions*, struct wav2prg_plugin_conf*, struct wav2prg_raw_block*, uint16_t);
typedef enum wav2prg_bool (*wav2prg_get_sync)(struct wav2prg_context*, const struct wav2prg_functions*, struct wav2prg_plugin_conf*, enum wav2prg_bool);
typedef enum wav2prg_sync_result (*wav2prg_get_sync_check)(struct wav2prg_context*, const struct wav2prg_functions*, struct wav2prg_plugin_conf*);
typedef enum wav2prg_bool (*wav2prg_get_block_info)(struct wav2prg_context*, const struct wav2prg_functions*, struct wav2prg_plugin_conf*, struct program_block_info*);
typedef enum wav2prg_checksum_state (*wav2prg_check_checksum)(struct wav2prg_context*, const struct wav2prg_functions*, struct wav2prg_plugin_conf*);
typedef void              (*wav2prg_reset_checksum_to)(struct wav2prg_context*, uint8_t);
typedef void              (*wav2prg_reset_checksum)(struct wav2prg_context*);
typedef uint8_t           (*wav2prg_compute_checksum_step)(struct wav2prg_plugin_conf*, uint8_t, uint8_t, uint16_t, uint32_t*);
typedef void              (*wav2prg_postprocess_and_update_checksum)(struct wav2prg_context*, struct wav2prg_plugin_conf*, uint8_t*, uint16_t);
typedef void              (*wav2prg_number_to_name)(uint8_t number, char* name);
typedef void              (*wav2prg_add_byte_to_block)(struct wav2prg_context*, struct wav2prg_raw_block* block, uint8_t byte);
typedef uint8_t           (*wav2prg_postprocess_data_byte)(struct wav2prg_plugin_conf*, uint8_t, uint16_t);

struct wav2prg_functions {
  wav2prg_get_sync get_sync;
  wav2prg_get_sync_check get_sync_sequence;
  wav2prg_get_pulse_func get_pulse_func;
  wav2prg_get_bit_func get_bit_func;
  wav2prg_get_byte_func get_byte_func;
  wav2prg_get_data_byte_func get_data_byte_func;
  wav2prg_get_word_func get_word_func;
  wav2prg_get_word_func get_data_word_func;
  wav2prg_get_word_func get_word_bigendian_func;
  wav2prg_get_block_func get_block_func;
  wav2prg_check_checksum check_checksum_func;
  wav2prg_reset_checksum_to reset_checksum_to_func;
  wav2prg_reset_checksum reset_checksum_func;
  wav2prg_number_to_name number_to_name_func;
  wav2prg_add_byte_to_block add_byte_to_block_func;
};

struct wav2prg_generate_private_state
{
  uint32_t size;
  const void* model;
};

struct wav2prg_plugin_functions {
  wav2prg_get_bit_func get_bit_func;
  wav2prg_get_byte_func get_byte_func;
  wav2prg_get_sync_check get_sync;
  wav2prg_get_byte_func get_first_byte_of_sync_sequence;
  wav2prg_get_block_info get_block_info;
  wav2prg_get_block_func get_block_func;
  wav2prg_compute_checksum_step compute_checksum_step;
  wav2prg_get_byte_func get_loaded_checksum_func;
  wav2prg_postprocess_data_byte postprocess_data_byte_func;
};

struct wav2prg_plugin_conf {
  enum wav2prg_plugin_endianness endianness;
  enum wav2prg_checksum checksum_type;
  enum wav2prg_checksum_computation checksum_computation;
  uint8_t num_extended_checksum_bytes;
  uint8_t num_pulse_lengths;
  uint16_t *thresholds;
  int16_t *pulse_length_deviations;
  enum wav2prg_findpilot_type findpilot_type;
  uint8_t pilot_byte;
  uint8_t len_of_sync_sequence;
  uint8_t *sync_sequence;
  uint32_t min_pilots;
  enum wav2prg_block_filling filling;
  enum wav2prg_bool opposite_waveform;
  void* private_state;
};

struct wav2prg_observer_functions;
struct wav2prg_observer_context;

typedef void                        (*wav2prg_change_sync_sequence_length)(struct wav2prg_plugin_conf*, uint8_t);
typedef void                        (*wav2prg_set_restart_point)(struct wav2prg_observer_context*, uint16_t);
typedef struct wav2prg_plugin_conf* (*wav2prg_observer_get_conf)(struct wav2prg_observer_context*);
typedef struct wav2prg_plugin_conf* (*wav2prg_observer_use_different_conf)(struct wav2prg_observer_context*, const struct wav2prg_plugin_conf*);
typedef void                        (*wav2prg_observer_set_info)(struct wav2prg_observer_context*, uint16_t, uint16_t, const char*);
typedef void                        (*wav2prg_disallow_gaps)(struct wav2prg_observer_context*);

typedef enum wav2prg_bool (*wav2prg_recognize_block)(struct wav2prg_observer_context*, const struct wav2prg_observer_functions*, const struct program_block*, uint16_t);

struct wav2prg_observer_functions {
  wav2prg_change_sync_sequence_length change_sync_sequence_length_func;
  wav2prg_set_restart_point set_restart_point_func;
  wav2prg_observer_get_conf get_conf_func;
  wav2prg_observer_use_different_conf use_different_conf_func;
  wav2prg_observer_set_info set_info_func;
  wav2prg_disallow_gaps disallow_gaps_func;
};

struct wav2prg_observer_loaders {
  const char* loader;
  const char* observation_description;
  wav2prg_recognize_block recognize_func;
};

struct wav2prg_context;

#define WAVPRG_LOADER_API {'B','L','4','0'}
#define WAVPRG_OBSERVER_API {'B','O','4','0'}

struct wav2prg_all_loaders {
  char api_version[4];
  struct {
    const char version[2];
    const char *desc;
  } loader_version;
  const struct wav2prg_loaders {
    const char *name;
    struct wav2prg_plugin_functions functions;
    struct wav2prg_plugin_conf conf;
  } *loaders;
};

struct wav2prg_all_observers {
  char api_version[4];
  const char version[2];
  const struct wav2prg_observers {
    const char* observed_name;
    struct wav2prg_observer_loaders observers;
  } *observers;
};


/* Meatloaf: static registration - loader plugins are linked into the
   firmware, so the registration macros generate uniquely-named globals
   that loaders.c registers explicitly. The WAV2PRG_OBSERVER macro takes
   an extra name argument compared to upstream wav-prg. */

#define WAV2PRG_LOADER(x, major,minor,desc, loaders) \
const struct wav2prg_all_loaders wav2prg_loader_##x = \
{ \
  WAVPRG_LOADER_API, \
  { \
    {major,minor}, \
     desc \
  }, \
  loaders \
};

#define WAV2PRG_OBSERVER(x, major,minor, observers) \
const struct wav2prg_all_observers wav2prg_observer_##x = \
{ \
  WAVPRG_OBSERVER_API, \
  {major,minor}, \
  observers \
};

#endif /* WAV2PRG_API_H */
