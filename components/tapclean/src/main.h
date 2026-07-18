/*
 * main.h
 *
 * Part of project "Final TAP".
 *
 * A Commodore 64 tape remastering and data extraction utility.
 *
 * (C) 2001-2006 Stewart Wilson, Subchrist Software.
 *
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
 * St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#ifdef WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <time.h>

typedef int (*readbyteproc_t)(int, int, int, int, int);

/* in main.c... */

int readttbit(int, int, int, int);
int readttbyte(int, int, int, int, int);
int find_pilot(int, int);
int find_pilot_bytes_ex(int, int, readbyteproc_t, int);
void calculate_averages_in_pilot(int start, int end, int threshold, int *sp, int *lp);
int load_tap(char *);
int analyze(void);
void report(void);
void clean(void);
int is_accounted(int);
int is_pause_param(int);
int find_decode_block(int, int);
int add_read_error(int);

/* general utilities... (could be put into their own file really) */

void msgout(char *);
int find_seq(int *, int, int *, int);
void getfilename(char *, char *);
char* change_file_extention(char *, char *, int);
char* pet2text(char *, char *);
void fname_text(char *);
void trim_string(char *);
void time_to_string(time_t, char *);

void delete_work_files(void);

/* in clean.c...  really an extension of main.c so no separate header */

void fix_header_size(void);
void clip_ends(void);
void unify_pauses(void);
void clean_files(void);
void fix_boot_pilot(void);
void convert_to_v1(void);
void convert_to_v0(void);
void standardize_pauses(void);
void fix_pilots(void);
void fix_prepausegaps(void);
void fix_postpausegaps(void);
int insert_pauses(void);
void cut_range(int, int);
void cut_postdata_gaps(void);
void cut_leading_gap(void);
void add_trailpause(void);
void fill_cbm_tone(void);
void fix_bleep_pilots(void);
void fix_pavloda_check_bytes(void);

/* in loader_id.c...  really an extension of main.c so no separate header */

int idloader(unsigned /*long*/ int, int);

/* in batchscan.c... really an extension of main.c so no separate header */

int batchscan(char *, int, int);
