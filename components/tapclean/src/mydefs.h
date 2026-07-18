/*
 * mydefs.h
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

#ifndef __MYDEFS_H__
#define __MYDEFS_H__

#include "database.h"
#include "scanners/_scanners.h"
#include "crc32.h"
#include "dc2nconv.h"

/* OS dependent slash */

#ifdef WIN32
#define SLASH	'\\'
#else
#define SLASH	'/'
#endif

#define VERSION_STR	"0.39-pre-7"
#define COPYRIGHT_STR	"(C)2006-2023 TC Team"
#define BUILDER_STR	"ldf"

#define TRUE	1
#define FALSE	0

#ifndef MAXPATH
#define MAXPATH	512
#endif
#define NUM_READ_ERRORS	100

#define CLEANED_PREFIX		"clean."
#define CONVERTED_PREFIX	"converted."

#define TAP_HEADER_SIZE 	20
#define TAP_ID_STRING		"C64-TAPE-RAW"

#define DC2N_HEADER_SIZE	20
#define DC2N_ID_STRING		"DC2N-TAP-RAW"

#define FAIL	0xFFFFFFFF
#define DEFTOL	11	/* default bit reading tolerance. (1 = zero tolerance) */
#define MAXTOL  16      /* max bit reading tolerance (as above, its bias is 1) */

#define LAME	0x0F	/* cutoff value for 'noise' pulses when rebuilding pauses.*/

/* CPU cycles per second for C64, C16 and VIC20 PAL and NTSC */

#define C64_PAL_CPS	985248
#define C64_NTSC_CPS	1022727
#define C16_PAL_CPS	886724
#define C16_NTSC_CPS	894886
#define VIC20_PAL_CPS	1108405
#define VIC20_NTSC_CPS	1022727

#define VIC20C16_BASIC_START_ADDR	0x1001
#define C64_BASIC_START_ADDR		0x0801

#define NA	-1	/* indicator: Not Applicable. */
#define VV	-1	/* indicator: A variable value is used. */
#define XX	-1	/* indicator: Dont care. */

#define LSbF	0	/* indicator: Least Significant bit First. */
#define MSbF	1	/* indicator: Most Significant bit First. */
#define ENDIANNESS_TO_STRING(en) ((en) == MSbF ? "MSbF" : "LSbF")

#define CSYES	1	/* indicator: A checksum is used. */
#define CSNO	0	/* indicator: A checksum is not used. */

#define OPC_ROL	0x26	/* 65xx ROL OPCode */
#define OPC_ROR	0x66	/* 65xx ROR OPCode */

#ifdef _MSC_VER	/* override POSIX names when using MSVC */
#define chdir	_chdir
#define getcwd	_getcwd
#define mkdir	_mkdir
#define stricmp	_stricmp
#define unlink	_unlink
#endif

/*
 * Each of these constants indexes an entry in the "ft[]" fmt_t array...
 * Note: the position of each enum IS relevant for uses in 'ft' array in main file.
 */

enum {
	LT_NONE=0,	/* Loader type that marks an empty slot in the "lt" field of the "blk[]" blk_t array */
	GAP=1,
	PAUSE,

	CBM_HEAD,
	CBM_DATA,
	TT_HEAD,
	TT_DATA,
	FREE,
	ODELOAD,
	FREEZEMACHINE,
	USGOLD,
	ACES,
	WILD,
	WILD_STOP,
	NOVA,
	NOVA_SPC,
	OCEAN_F1,
	OCEAN_F2,
	OCEAN_F3,
	MEGASAVE_T1,
	MEGASAVE_T2,
	MEGASAVE_T3,
	MEGASAVE_T4,
	RASTER,
	CYBER_F1,
	CYBER_F2,
	CYBER_F3,
	CYBER_F4_1,
	CYBER_F4_2,
	CYBER_F4_3,
	BLEEP,
	BLEEP_TRIG,
	BLEEP_SPC,
	HITLOAD,
	MICROLOAD,
	BURNER,
	RACKIT,
	SPAV1_HD,
	SPAV1,
	SPAV2_HD,
	SPAV2,
	VIRGIN,
	HITEC,
	ANIROG,
	VISI_T1,
	VISI_T2,
	VISI_T3,
	VISI_T4,
	VISI_T5,
	VISI_T6,
	VISI_T7,
	SUPERTAPE_HEAD,
	SUPERTAPE_DATA,
	PAV,
	IK,
	FBIRD_T1,
	FBIRD_T2,
	TURR_HEAD,
	TURR_DATA,
	SEUCK_L2,
	SEUCK_HEAD,
	SEUCK_DATA,
	SEUCK_TRIG,
	SEUCK_GAME,
	JET,
	FLASH,
	TDI_F1,
	OCNEW1_T1,
	OCNEW1_T2,
	OCNEW2,
	ATLAN,
	SNAKE51,
	SNAKE50_T1,
	SNAKE50_T2,
	PAL_F1,
	PAL_F2,
	ENIGMA,
	AUDIOGENIC,
	ALIENSY,
	ACCOLADE,
	ALTERWG,
	RAINBOWARTS_F1,
	RAINBOWARTS_F2,
	TRILOGIC,
	BURNERVAR,
	OCNEW4,
	TDI_F2,
	BITURBO,
	T108DE0A5,
	ACTIONREPLAY_HDR,
	ACTIONREPLAY_TURBO,
	ACTIONREPLAY_STURBO,
	ASHDAVE,
	FREE_SLOW_T1,
	FREE_SLOW_T2,
	GOFORGOLD,
	JIFFYLOAD_T1,
	JIFFYLOAD_T2,
	FFTAPE,
	TESTAPE,
	TEQUILA,
	GRADVCREATOR,
	CHUCKIEEGG,
	ALTERDK_T1,
	ALTERDK_T2,
	ALTERDK_T3,
	ALTERDK_T4,
	POWERLOAD,
	GREMLIN_F1,
	GREMLIN_F2,
	AMACTION,
	CREATURES,
	RAINBOW_ISLANDS,
	OCNEW3,
	EASYTAPE,
	TURBO220,
	CSPARKS,
	DIGITAL_DESIGN,
	GLASS_HEAD,
	GLASS_DATA,
	TT526_HEAD,
	TT526_DATA,
	MICROLOADVAR_T1,
	MICROLOADVAR_T2,
	LEXPEED,
	MMS,
	GREMLIN_GBH_HEAD,
	GREMLIN_GBH_DATA,
	LK_AVALON,
	TT263_HEAD,
	TT263_DATA,
	GYROSPEED,
	MSX_HEAD,
	MSX_DATA,
	MSX_HEAD_FAST,
	MSX_DATA_FAST,
	TTFAST_HEAD,
	TTFAST_DATA
};

/*
 * These constants are the loader IDs used for quick scanning via CRC lookup or
 * program data pattern lookup... See file "loader_id.c"
 * Note: the position of each enum IS relevant for uses in 'knam' array in main file.
 */

enum {
	LID_NONE=0,
	LID_FREE,
	LID_ODE,
	LID_BLEEP,
	LID_MEGASAVE,
	LID_BURN,
	LID_WILD,
	LID_USG,
	LID_MIC,
	LID_ACE,
	LID_T250,
	LID_RACK,
	LID_OCEAN,
	LID_RAST,
	LID_SPAV,
	LID_HIT,
	LID_ANI,
	LID_VIS_T1,
	LID_VIS_T2,
	LID_VIS_T3,
	LID_VIS_T4,
	LID_VIS_T5,
	LID_VIS_T6,
	LID_VIS_T7,
	LID_FIRE,
	LID_NOVA,
	LID_IK,
	LID_PAV,
	LID_CYBER,
	LID_VIRG,
	LID_HTEC,
	LID_FLASH,
	LID_SUPER,
	LID_OCNEW1_T1,
	LID_OCNEW1_T2,
	LID_ATLAN,
	LID_SNAKE,
	LID_OCNEW2,
	LID_AUDIOGENIC,
	LID_FRZMACHINE,
	LID_ACCOLADE,
	LID_RAINBOWARTS,
	LID_BURNERVAR,
	LID_OCNEW4,
	LID_108DE0A5,
	LID_FREE_SLOW,
	LID_GOFORGOLD,
	LID_JIFFYLOAD,
	LID_FFTAPE,
	LID_TESTAPE,
	LID_TEQUILA,
	LID_GRADVCREATOR,
	LID_CHUCKIEEGG,
	LID_ALTERDK,
	LID_POWERLOAD,
	LID_GREMLIN,
	LID_EASYTAPE,
	LID_CSPARKS,
	LID_TRILOGIC,
	LID_GLASS,
	LID_MICVAR,
	LID_LEXPEED,
	LID_MMS,
	LID_GREMLINGBH,
	LID_GYROSPEED,
};

/*
 * These constants are loader IDs for extra one-off loaders, used for quick
 * scanning via header pattern lookup... See file "loader_id.c"
 */
enum {
	LIDEX_NONE=0
};


/* struct 'tap_t' contains general info about the loaded tap file... */

struct tap_t
{
	char path[MAXPATH];	/* file path + name. */
	char name[MAXPATH];	/* file name. */
	unsigned char *tmem;	/* storage for the loaded tap. */
	int len;		/* length of the loaded tap. */
	int pst[256];		/* pulse stats table. */
	int fst[256];		/* file stats table. */
	int fsigcheck;		/* header check results... (1=ok, 0=failed) */
	int fvercheck;
	int fsizcheck;
	int detected;		/* number of bytes accounted for. */
	int detected_percent;	/* ..and as a percentage.*/
	int purity;		/* number of pulse types in the tap. */
	int total_gaps;		/* number of gaps. */
	int total_data_files;	/* number of files found. (not inc pauses and gaps) */
	int total_checksums;	/* number of files found with checksums. */
	int total_checksums_good;	/* number of checksums verified. */
	int optimized_files;	/* number of fully optimized files. */
	int total_read_errors;	/* number of read errors. */
	int fdate;		/* age of file. (date and time stamp). */
	float taptime;		/* playing time (seconds). */
	int version;		/* TAP version (currently 0 or 1). */
	int bootable;		/* holds the number of bootable ROM file sequences */
	int changed;		/* flags that the tap has been altered (+ needs rescan) */
	unsigned /*long*/ int crc;	/* overall (data extraction) crc32 for this tap */
	unsigned /*long*/ int cbmcrc;	/* crc32 of the 1st CBM program found */
	unsigned /*long*/ int cbmhcrc;	/* crc32 of the 1st CBM header found */
	int cbmdatalen;		/* length of the 1st CBM program whose crc32 is stored in cbmcrc */
	int cbmid;		/* loader id, see enums in mydefs.h (-1 = N/A) */
	char cbmname[20];	/* filename for first CBM file (if exists). */
	int tst_hd;
	int tst_rc;
	int tst_op;		/* quality test results. 0=Pass, 1= Fail */
	int tst_cs;
	int tst_rd;
};
#ifdef TAPCLEAN_EMBEDDED
/* The ~3.6 KB tap struct is heap-allocated (PSRAM) by tapclean_init()
   instead of sitting in internal-DRAM BSS; the macro keeps every use
   site (tap.field) unchanged. */
extern struct tap_t *tapclean_tap;
#define tap (*tapclean_tap)
#else
extern struct tap_t tap;
#endif


/* a reduced version of the above used as a database by batch scan... */

struct tap_tr
{
	char path[MAXPATH];	/* full file path + name. */
	char name[MAXPATH];	/* file name. */
	int len;		/* length of the loaded tap. */
	int detected_percent;	/* ..and as a percentage. */
	int purity;		/* number of pulse types in the tap. */
	int total_data_files;	/* not including pauses or gaps. */
	int total_gaps;		/* number of gaps. */
	int fdate;		/* age of file. (date and time stamp). */
	int version;		/* TAP version (currently 0 or 1). */
	unsigned /*long*/ int crc;	/* overall (data extraction) crc32 for this tap */
	unsigned /*long*/ int cbmcrc;	/* crc32 of the 1st CBM program found  */
	int cbmid;		/* loader id, see enums in mydefs.h (-1 = N/A) */
	char cbmname[20];	/* filename for first CBM file (if exists). */
	int tst_hd;
	int tst_rc;
	int tst_op;		/* quality test results. 0=Pass, 1= Fail */
	int tst_cs;
	int tst_rd;
};


/* struct 'fmt_t' contains info about a particular tape format... */

struct fmt_t
{
	char *name;		/* format name */
	int en;			/* byte endianness, 0=LSbF, 1=MSbF */
	int tp;			/* threshold pulsewidth (if applicable) */
	int sp;			/* ideal short pulsewidth */
	int mp;			/* ideal medium pulsewidth (if applicable) */
	int lp;			/* ideal long pulsewidth */
	int pv;			/* pilot value */
	int sv;			/* sync value */
	int pmin;		/* minimum pilots that should be present. */
	int pmax;		/* maximum pilots that should be present. */
	int has_cs;		/* flag, provides checksums, 1=yes, 0=no. */
};
#ifdef TAPCLEAN_EMBEDDED
extern struct fmt_t *ft;	/* PSRAM working copy, see tapclean_init() */
#else
extern struct fmt_t ft[];
#endif

extern unsigned char cbm_header[192];		/* some formats must have their loader... */
#ifdef TAPCLEAN_EMBEDDED
extern unsigned char *cbm_program;	/* heap-allocated by tapclean_init() */
#else
extern unsigned char cbm_program[65536];	/* interrogated. */
#endif
extern int cbm_decoded;				/* 1= yes, 0= no */

extern int aborted;		/* general 'operation aborted' flag */
extern int tol;			/* turbotape bit reading tolerance. */
#ifdef TAPCLEAN_EMBEDDED
extern char *lin;		/* heap-allocated by tapclean_init() */
extern char *info;		/* heap-allocated by tapclean_init() */
#else
extern char lin[64000];		/* general purpose string building buffer */
extern char info[1048576];	/* string building buffer, gathers output from scanners */
#endif

extern char c16;
extern char c20;
extern char c64;

extern const char knam[][48];

extern int quiet;

extern int batchmode;

/* These are defined in main.c (move to main.h and include main.h here) */

extern const char temptcbatchreportname[];
extern const char tcbatchreportname[];
extern char exedir[MAXPATH];	/* assigned in main.c, includes trailing slash. */


/* program options... */

#ifdef TAPCLEAN_EMBEDDED
/* Rename short global names that collide with application symbols
   (lib/utils 'tmp', lib/sam 'debug'). Consistent across all engine
   sources since everything includes mydefs.h. */
#define debug tapclean_option_debug
#define tmp tapclean_buf_tmp

/* Route every engine malloc() to PSRAM: the scan makes thousands of
   small allocations (blk_t structs, decoded data blocks) that would
   otherwise land in - and exhaust - internal DRAM. free() is unchanged
   (unified heap). */
#include <stddef.h>
void *tapclean_psram_malloc(size_t n);
#define malloc tapclean_psram_malloc

/* Size-optimize the whole engine regardless of the app build type: the
   app's flash text must stay inside the 3.3 MB instruction window, and
   the PlatformIO ESP-IDF builder ignores per-component -O options. Every
   engine source includes this header before defining functions. */
#pragma GCC optimize("Os")
#endif

extern char debug;
extern char noid;
extern char noc64eof;
extern char docyberfault;
extern char boostclean;
extern char prgunite;
extern char extvisipatch;
extern char incsubdirs;
extern char sortbycrc;
extern char exportcyberloaders;
extern char skewadapt;

struct ldrswt_t		/* Loader -no/-do switches */
{
	char desc[32];	/* Human readable description of loader */
	char par[16];	/* Whatever can follow the "-do" and "-no" prefixes
			   to include or exclude a loader */
	char exclude;	/* Set to TRUE to exclude loader */
};

/* Note: the position of each enum IS relevant for uses in 'ldrswt' array in main file. */
enum {
	noc64 = 0,
	no108DE0A5,
	noaccolade,
	noaces,
	noar,
	noaliensy,
	noalterdk,
	noalterwg,
	noamaction,
	noanirog,
	noashdave,
	noatlantis,
	noaudiogenic,
	nobiturbo,
	nobleep,
	noburner,
	noburnervar,
	nochuckie,
	nocsparks,
	nocreatures,
	nocyber,
	noddesign,
	noeasytape,
	noenigma,
	nofftape,
	nofire,
	noflash,
	nofree,
	nofrslow,
	nofrzmachine,
	noglass,
	nogoforgold,
	nogradvcreator,
	nogremlinf1,
	nogremlinf2,
	nogremlingbh,
	nogyrospeed,
	nohit,
	nohitec,
	noik,
	nojet,
	nojiffyload,
	nolexpeed,
	nolkavalon,
	nomegasave,
	nomicro,
	nomicrovar,
	nomms,
	nomsx,
	nonova,
	noocean,
	nooceannew1,
	nooceannew2,
	nooceannew3,
	nooceannew4,
	noode,
	nopalacef1,
	nopalacef2,
	nopav,
	nopower,
	norackit,
	norainbowf1,
	norainbowf2,
	norislands,
	noraster,
	noseuck,
	nosnake50,
	nosnake51,
	nospav,
	nosuper,
	notdif1,
	notdif2,
	notequila,
	notestape,
	notrilogic,
	noturbo220,
	noturbo,
	noturbo263,
	noturbo526,
	noturr,
	nousgold,
	novirgin,
	novisi,
	nowild,
	noturbofast
};

enum {
	OP_NONE = 0,
	OP_TEST,
	OP_OPTIMIZE,
	OP_CONVERT_V0,
	OP_CONVERT_V1,
	OP_FIX_HEADER_SIZE,
	OP_PAUSE_OPTIMIZE,
	OP_CONVERT_AU,
	OP_CONVERT_WAV,
	OP_BATCH_SCAN,
	OP_CREATE_INFO
};

#endif /* __MYDEFS_H__ */
