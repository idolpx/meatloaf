/*
 * main.c  (Final TAP 2.7.x console version).
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

#ifdef linux
#define _GNU_SOURCE
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#include "main.h"
#include "mydefs.h"
#include "crc32.h"
#include "skewadapt.h"
#ifndef TAPCLEAN_EMBEDDED
#include "tap2audio.h"
#include "persistence.h"
#endif

/* program options... */

static char doprg		= FALSE;
static char noaddpause		= FALSE;
static char reckless		= FALSE;
static char sine		= FALSE;

char debug			= FALSE;
char noid			= FALSE;
char noc64eof			= FALSE;
char docyberfault		= FALSE;
char boostclean			= FALSE;
char prgunite			= FALSE;
char extvisipatch		= FALSE;
char incsubdirs			= FALSE;
char sortbycrc			= FALSE;
char exportcyberloaders		= FALSE;
char skewadapt			= FALSE;
char fstats			= FALSE;
char preserveloadervars		= FALSE;

char c16			= FALSE;
char c20			= FALSE;
char c64			= TRUE;
char pal			= TRUE;
char ntsc			= FALSE;

/*
 * Parameters -no/-do and descriptions
 *
 * See enum for this table in mydefs.h: these must be kept in sync.
 */
#ifdef TAPCLEAN_EMBEDDED
/* never written in the embedded build (the -no/-do CLI options are
   compiled out) - const keeps the ~4.4 KB table in flash, not DRAM */
const
#endif
struct ldrswt_t ldrswt[] = {
	/* description (max 31 chars),   parameter (max 15 chars),     exclude? */
	{"C64 ROM loader"		,"c64"		,FALSE},
	{"108DE0A5"			,"108DE0A5"	,FALSE},
	{"Accolade/EA"			,"accolade"	,FALSE},
	{"Ace of Aces"			,"aces"		,FALSE},
	{"ActionReplay"			,"ar"		,FALSE},
	{"Alien Syndrome"		,"aliensy"	,FALSE},
	{"Alternative SW (DK)"		,"alterdk"	,FALSE},
	{"Alternative World Games"	,"alterwg"	,FALSE},
	{"American Action"		,"amaction"	,FALSE},
	{"Anirog"			,"anirog"	,FALSE},
	{"Ash+Dave"			,"ashdave"	,FALSE},
	{"Atlantis"			,"atlantis"	,FALSE},
	{"Audiogenic"			,"audiogenic"	,FALSE},
	{"Biturbo"			,"biturbo"	,FALSE},
	{"Bleepload"			,"bleep"	,FALSE},
	{"Burner"			,"burner"	,FALSE},
	{"Burner Variant"		,"burnervar"	,FALSE},
	{"Chuckie Egg"			,"chuckie"	,FALSE},
	{"Creative Sparks"		,"csparks"	,FALSE},
	{"Creatures"			,"creatures"	,FALSE},
	{"Cyberload"			,"cyber"	,FALSE},
	{"Digital Design"		,"ddesign"	,FALSE},
	{"Easy-Tape"			,"easytape"	,FALSE},
	{"Enigma"			,"enigma"	,FALSE},
	{"Freeze Frame Tape"		,"fftape"	,FALSE},
	{"Firebird"			,"fire"		,FALSE},
	{"Flashload"			,"flash"	,FALSE},
	{"Freeload"			,"free"		,FALSE},
	{"Freeload Slowload"		,"frslow"	,FALSE},
	{"Freeze Machine tape"		,"frzmachine"	,FALSE},
	{"Glass Tape"			,"glass"	,FALSE},
	{"Go For The Gold"		,"goforgold"	,FALSE},
	{"GAC Save tape"		,"gradvcreator"	,FALSE},
	{"Gremlin F1"			,"gremlinf1"	,FALSE},
	{"Gremlin F2"			,"gremlinf2"	,FALSE},
	{"Gremlin GBH"			,"gremlingbh"	,FALSE},
	{"Gyrospeed"			,"gyrospeed"	,FALSE},
	{"Hitload"			,"hit"		,FALSE},
	{"Hi-Tec"			,"hitec"	,FALSE},
	{"IK"				,"ik"		,FALSE},
	{"Jetload"			,"jet"		,FALSE},
	{"Jiffy Load"			,"jiffy"	,FALSE},
	{"Lexpeed"			,"lexpeed"	,FALSE},
	{"LK Avalon"			,"lkavalon"	,FALSE},
	{"Mega-Save"			,"megasave"	,FALSE},
	{"Microload"			,"micro"	,FALSE},
	{"Microload Variant"		,"microvar"	,FALSE},
	{"MMS"				,"mms"		,FALSE},
	{"MSX"				,"msx"		,FALSE},
	{"Novaload"			,"nova"		,FALSE},
	{"Ocean"			,"ocean"	,FALSE},
	{"Ocean New 1"			,"oceannew1"	,FALSE},
	{"Ocean New 2"			,"oceannew2"	,FALSE},
	{"Ocean New 3"			,"oceannew3"	,FALSE},
	{"Ocean New 4"			,"oceannew4"	,FALSE},
	{"ODEload"			,"ode"		,FALSE},
	{"Palace F1"			,"palacef1"	,FALSE},
	{"Palace F2"			,"palacef2"	,FALSE},
	{"Pavloda"			,"pav"		,FALSE},
	{"Power Load"			,"power"	,FALSE},
	{"Rack-It"			,"rackit"	,FALSE},
	{"Rainbow Arts F1"		,"rainbowf1"	,FALSE},
	{"Rainbow Arts F2"		,"rainbowf2"	,FALSE},
	{"Rainbow Islands"		,"rislands"	,FALSE},
	{"Rasterload"			,"raster"	,FALSE},
	{"SEUCK"			,"seuck"	,FALSE},
	{"Snakeload 50"			,"snake50"	,FALSE},
	{"Snakeload 51"			,"snake51"	,FALSE},
	{"Super Pavloda"		,"spav"		,FALSE},
	{"Super Tape"			,"super"	,FALSE},
	{"TDI F1"			,"tdif1"	,FALSE},
	{"TDI F2"			,"tdif2"	,FALSE},
	{"Tequila Sunrise"		,"tequila"	,FALSE},
	{"TES Tape"			,"testape"	,FALSE},
	{"Trilogic"			,"trilogic"	,FALSE},
	{"Turbo 220"			,"turbo220"	,FALSE},
	{"Turbotape 250"		,"turbo"	,FALSE},
	{"Turbotape 263"		,"turbo263"	,FALSE},
	{"Turbotape 526"		,"turbo526"	,FALSE},
	{"Turrican"			,"turr"		,FALSE},
	{"U.S. Gold"			,"usgold"	,FALSE},
	{"Virgin"			,"virgin"	,FALSE},
	{"Visiload"			,"visi"		,FALSE},
	{"Wildload"			,"wild"		,FALSE},
	{"Turbotape 64 Fast"		,"turbofast"	,FALSE}
	/* description (max 31 chars),   parameter (max 15 chars),     exclude? */
};

static int read_errors[NUM_READ_ERRORS];	/* storage for 1st NUM_READ_ERRORS read error addresses */
static char note_errors;	/* set true only when decoding identified files, */
				/* it just tells 'add_read_error()' to ignore. */

#ifdef TAPCLEAN_EMBEDDED
struct tap_t *tapclean_tap;	/* allocated by tapclean_init(); 'tap' is a
				   macro for (*tapclean_tap), see mydefs.h */
#else
struct tap_t tap;		/* container for the loaded tap (note: only ONE tap). */
#endif

int tol = DEFTOL;		/* bit reading tolerance. (1 = zero tolerance) */

#ifdef TAPCLEAN_EMBEDDED
/* Embedded build: these work buffers are far too large for static RAM on a
   microcontroller. They are heap-allocated (PSRAM-first on ESP32) by
   tapclean_init() in tapclean_api.c, and 'info' is reset per block in
   describe_blocks() since the full-report use case does not exist here. */
char *info;
char *lin;
char *tmp;
#else
char info[1048576];		/* buffer area for storing each blocks description text. */
				/* also used to store the database report, hence the size! (1MB). */
char lin[64000];		/* general purpose string building buffer. */
char tmp[64000];		/* general purpose string building buffer. */
#endif

int aborted = FALSE;		/* general 'operation aborted by user' flag. */

int batchmode = FALSE;		/* set to 1 when "batch analysis" is performed. */
				/* set to 0 when the user Opens an individual tap. */

unsigned char cbm_header[192];	/* these will allow some interrogation of the CBM parts */
#ifdef TAPCLEAN_EMBEDDED
unsigned char *cbm_program;	/* heap-allocated by tapclean_init() */
#else
unsigned char cbm_program[65536];	/* that some customized loaders may rely on. (ie. burner). */
#endif
int cbm_decoded = FALSE;	/* this ensures only *1st* cbm parts are decoded. */

int quiet = FALSE;		/* set 1 to stop the loader search routines from producing output, */
				/* ie. "Scanning: Freeload". */
				/* i set it (1) when (re)searching during optimization. */

long cps = C64_PAL_CPS;		/* CPU Cycles pr second. Default is C64 PAL */


/*
 * Where a field is marked 'VV', loader/file interrogation is required to
 * discover the missing value.
 *
 * NOTE: some of the values (like number of pilot bytes) may not agree with
 * the loader docs, this is done to let partly damaged games be detected
 * and fixed.
 *
 * en = byte endianness, 0=LSbF, 1=MSbF
 * tp = threshold pulsewidth (if applicable)
 * sp = ideal short pulsewidth
 * mp = ideal medium pulsewidth (if applicable)
 * lp = ideal long pulsewidth
 * pv = pilot value
 * sv = sync value
 * pmin = minimum pilots that should be present.
 * pmax = maximum pilots that should be present.
 * has_cs = flag, provides checksums, 1=yes, 0=no.
 *
 * NOTE: the recommended value for pmin is 1/2 of the pilot size usually found 
 * on TAPs for very short pilot sequences (e.g. 8 bytes) and 3/4 of the pilot 
 * size for longer pilot sequences.
 */

#ifdef TAPCLEAN_EMBEDDED
/* The working table is a PSRAM copy made by tapclean_init() (scanners
   write the VV fields at runtime, so it cannot be const, and ~4.3 KB of
   internal-DRAM .data is too expensive); these defaults stay in flash. */
static const struct fmt_t ft_defaults[] = {
#else
struct fmt_t ft[] = {
#endif
	/* name,                 en,   tp,   sp,   mp,   lp,   pv,   sv,   pmin, pmax,  has_cs. */

	{""			,NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,    NA},
	{"UNRECOGNIZED"		,NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,    NA},
	{"PAUSE"		,NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,    NA},

	{"C64 ROM-TAPE HEADER"	,LSbF, NA,   0x30, 0x42, 0x56, NA,   NA,   50,   NA,    CSYES},
	{"C64 ROM-TAPE DATA"	,LSbF, NA,   0x30, 0x42, 0x56, NA,   NA,   50,   NA,    CSYES},
	{"TURBOTAPE-250 HEADER"	,MSbF, 0x20, 0x1A, NA,   0x28, 0x02, 0x09, 50,   NA,    CSNO},
	{"TURBOTAPE-250 DATA"	,MSbF, 0x20, 0x1A, NA,   0x28, 0x02, 0x09, 50,   NA,    CSYES},
	{"FREELOAD"		,MSbF, 0x2C, 0x24, NA,   0x42, 0x40, 0x5A, 45,   400,   CSYES},
	{"ODELOAD"		,MSbF, 0x36, 0x25, NA,   0x50, 0x20, 0xDB, 40,   NA,    CSYES},
	{"FREEZE MACHINE TAPE"	,LSbF, 0x34, 0x27, NA,   0x3E, 0,    1,    1000, NA,    CSNO},
	{"US-GOLD TAPE"		,MSbF, 0x2C, 0x24, NA,   0x42, 0x20, 0xFF, 50,   NA,    CSYES},
	{"ACE OF ACES TAPE"	,MSbF, 0x2C, 0x22, NA,   0x47, 0x80, 0xFF, 50,   NA,    CSYES},
	{"WILDLOAD"		,LSbF, 0x3B, 0x30, NA,   0x47, 0xA0, 0x0A, 50,   NA,    CSYES},
	{"WILDLOAD STOP"	,LSbF, 0x3B, 0x30, NA,   0x47, 0xA0, 0x0A, 50,   NA,    CSNO},
	{"NOVALOAD"		,LSbF, 0x3D, 0x24, NA,   0x56, 0,    1,    1800, NA,    CSYES},
	{"NOVALOAD SPECIAL"	,LSbF, 0x3D, 0x24, NA,   0x56, 0,    1,    1800, NA,    CSYES},
	{"OCEAN/IMAGINE F1"	,LSbF, 0x3B, 0x24, NA,   0x56, 0,    1,    3000, 13000, CSNO},
	{"OCEAN/IMAGINE F2"	,LSbF, 0x3B, 0x24, NA,   0x56, 0,    1,    3000, 13000, CSNO},
	{"OCEAN/IMAGINE F3"	,LSbF, 0x3B, 0x24, NA,   0x56, 0,    1,    3000, 13000, CSNO},
	{"MEGA-SAVE T1"		,MSbF, 0x20, 0x1A, NA,   0x28, 0x63, 0x64, 50,   NA,    CSYES},
	{"MEGA-SAVE T2"		,MSbF, 0x2D, 0x26, NA,   0x36, 0x63, 0x64, 50,   NA,    CSYES},
	{"MEGA-SAVE T3"		,MSbF, 0x3E, 0x36, NA,   0x47, 0x63, 0x64, 50,   NA,    CSYES},
	{"MEGA-SAVE T4"		,MSbF, 0x6B, 0x56, NA,   0xA9, 0x63, 0x64, 50,   NA,    CSYES},
	{"RASTERLOAD"		,MSbF, 0x3F, 0x26, NA,   0x58, 0x80, 0xFF, 20,   NA,    CSYES},
	{"CYBERLOAD F1"		,MSbF, VV,   VV,   VV,   VV,   VV,   VV,   50,   NA,    CSNO},
	{"CYBERLOAD F2"		,MSbF, VV,   VV,   VV,   VV,   VV,   VV,   20,   NA,    CSNO},
	{"CYBERLOAD F3"		,MSbF, VV,   VV,   VV,   VV,   VV,   VV,   7,    9,     CSYES},
	{"CYBERLOAD F4_1"	,MSbF, VV,   VV,   VV,   VV,   VV,   VV,   6,    11,    CSYES},
	{"CYBERLOAD F4_2"	,MSbF, VV,   VV,   VV,   VV,   VV,   VV,   6,    11,    CSYES},
	{"CYBERLOAD F4_3"	,MSbF, VV,   VV,   VV,   VV,   VV,   VV,   6,    11,    CSYES},
	{"BLEEPLOAD"		,MSbF, 0x45, 0x30, NA,   0x5A, VV,   VV,   5,    NA,    CSYES},
	{"BLEEPLOAD TRIGGER"	,MSbF, 0x45, 0x30, NA,   0x5A, VV,   VV,   5,    NA,    CSNO},
	{"BLEEPLOAD SPECIAL"	,MSbF, 0x45, 0x30, NA,   0x5A, VV,   VV,   5,    NA,    CSYES},
	{"HITLOAD"		,MSbF, 0x4E, 0x34, NA,   0x66, 0x40, 0x5A, 30,   NA,    CSYES},
	{"MICROLOAD"		,LSbF, 0x29, 0x1C, NA,   0x36, 0xA0, 0x0A, 50,   NA,    CSYES},
	{"BURNER TAPE"		,VV,   0x2F, 0x22, NA,   0x42, VV,   VV,   30,   NA,    CSNO},
	{"RACK-IT TAPE"		,MSbF, 0x2B, 0x1D, NA,   0x3D, VV,   VV,   50,   NA,    CSYES},
	{"SUPERPAV T1 HEADER"	,MSbF, NA,   0x2E, 0x45, 0x5C, NA,   0x66, 50,   NA,    CSYES},
	{"SUPERPAV T1"		,MSbF, NA,   0x2E, 0x45, 0x5C, NA,   0x66, 50,   NA,    CSYES},
	{"SUPERPAV T2 HEADER"	,MSbF, NA,   0x3E, 0x5D, 0x7C, NA,   0x66, 50,   NA,    CSYES},
	{"SUPERPAV T2"		,MSbF, NA,   0x3E, 0x5D, 0x7C, NA,   0x66, 50,   NA,    CSYES},

	/* name,                 en,   tp,   sp,   mp,   lp,   pv,   sv,   pmin, pmax,  has_cs. */

	{"VIRGIN TAPE"		,MSbF, 0x2B, 0x21, NA,   0x3B, 0xAA, 0xA0, 30,   NA,    CSYES},
	{"HI-TEC TAPE"		,MSbF, 0x2F, 0x25, NA,   0x45, 0xAA, 0xA0, 30,   NA,    CSYES},
	{"ANIROG TAPE"		,MSbF, 0x20, 0x1A, NA,   0x28, 0x02, 0x09, 50,   NA,    CSNO},
	{"VISILOAD T1"		,VV,   0x35, 0x25, NA,   0x48, 0x00, 0x16, 100,  NA,    CSNO},
	{"VISILOAD T2"		,VV,   0x3B, 0x25, NA,   0x4B, 0x00, 0x16, 100,  NA,    CSNO},
	{"VISILOAD T3"		,VV,   0x3E, 0x25, NA,   0x54, 0x00, 0x16, 100,  NA,    CSNO},
	{"VISILOAD T4"		,VV,   0x47, 0x30, NA,   0x5D, 0x00, 0x16, 100,  NA,    CSNO},
	{"VISILOAD T5"		,VV,   0x52, 0x37, NA,   0x6B, 0x00, 0x16, 100,  NA,    CSNO},
	{"VISILOAD T6"		,VV,   0x2B, 0x1C, NA,   0x37, 0x00, 0x16, 100,  NA,    CSNO},
	{"VISILOAD T7"		,VV,   0x44, 0x2D, NA,   0x5A, 0x00, 0x16, 100,  NA,    CSNO},
	{"SUPERTAPE HEADER"	,LSbF, NA,   0x21, 0x32, 0x43, 0x16, 0x2A, 50,   NA,    CSYES},
	{"SUPERTAPE DATA"	,LSbF, NA,   0x21, 0x32, 0x43, 0x16, 0xC5, 50,   NA,    CSYES},
	{"PAVLODA"		,MSbF, 0x28, 0x1F, NA,   0x3F, 0,    1,    50,   NA,    CSYES},
	{"IK TAPE"		,MSbF, 0x2C, 0x27, NA,   0x3F, 1,    0,    1000, NA,    CSYES},
	{"FIREBIRD T1"		,LSbF, 0x62, 0x44, NA,   0x7E, 0x02, 0x52, 30,   NA,    CSYES},
	{"FIREBIRD T2"		,LSbF, 0x52, 0x45, NA,   0x65, 0x02, 0x52, 30,   NA,    CSYES},
	{"TURRICAN TAPE HEADER"	,MSbF, NA,   0x1B, NA,   0x27, 0x02, 0x0C, 30,   NA,    CSNO},
	{"TURRICAN TAPE DATA"	,MSbF, NA,   0x1B, NA,   0x27, 0x02, 0x0C, 30,   NA,    CSYES},
	{"SEUCK LOADER 2"	,LSbF, 0x2D, 0x20, NA,   0x41, 0xE3, 0xD5, 5,    NA,    CSYES},
	{"SEUCK HEADER"		,LSbF, 0x2D, 0x20, NA,   0x41, 0xE3, 0xD5, 5,    NA,    CSNO},
	{"SEUCK SUB-BLOCK"	,LSbF, 0x2D, 0x20, NA,   0x41, 0xE3, 0xD5, 5,    NA,    CSYES},
	{"SEUCK TRIGGER"	,LSbF, 0x2D, 0x20, NA,   0x41, 0xE3, 0xD5, 5,    NA,    CSNO},
	{"SEUCK GAME"		,LSbF, 0x2D, 0x20, NA,   0x41, 0xE3, 0xAC, 50,   NA,    CSNO},
	{"JETLOAD"		,LSbF, 0x3B, 0x33, NA,   0x58, 0xD1, 0x2E, 1,    NA,    CSNO},
	{"FLASHLOAD"		,MSbF, NA,   0x1F, NA,   0x31, 1,    0,    50,   NA,    CSYES},
	{"TDI TAPE F1"		,LSbF, NA,   0x44, NA,   0x65, 0xA0, 0x0A, 50,   NA,    CSYES},
	{"OCEAN NEW TAPE F1 T1"	,MSbF, NA,   0x22, NA,   0x42, 0x40, 0x5A, 50,   200,   CSYES},
	{"OCEAN NEW TAPE F1 T2"	,MSbF, NA,   0x35, NA,   0x65, 0x40, 0x5A, 50,   200,   CSYES},
	{"OCEAN NEW TAPE F2"	,MSbF, 0x2C, 0x22, NA,   0x42, 0x40, 0x5A, 50,   200,   CSYES},
	{"ATLANTIS TAPE"	,LSbF, 0x2F, 0x1D, NA,   0x42, 0x02, 0x52, 50,   NA,    CSYES},
	{"SNAKELOAD 5.1"	,MSbF, NA,   0x28, NA,   0x48, 0,    1,    1800, NA,    CSYES},
	{"SNAKELOAD 5.0 T1"	,MSbF, NA,   0x3F, NA,   0x5F, 0,    1,    1800, NA,    CSYES},
	{"SNAKELOAD 5.0 T2"	,MSbF, NA,   0x60, NA,   0xA0, 0,    1,    1800, NA,    CSYES},
	{"PALACE TAPE F1"	,MSbF, NA,   0x30, NA,   0x57, 0x01, 0x4A, 50,   NA,    CSYES},
	{"PALACE TAPE F2"	,MSbF, NA,   0x30, NA,   0x57, 0x01, 0x4A, 50,   NA,    CSYES},
	{"ENIGMA TAPE"		,MSbF, 0x2C, 0x24, NA,   0x42, 0x40, 0x5A, 700,  NA,    CSNO},
	{"AUDIOGENIC"		,MSbF, 0x28, 0x1A, NA,   0x36, 0xF0, 0xAA, 4,    NA,    CSYES},
	{"ALIEN SYNDROME"	,MSbF, 0x2C, 0x20, NA,   0x43, 0xE3, 0xED, 4,    NA,    CSYES},
	{"ACCOLADE"		,MSbF, 0x3D, 0x29, NA,   0x4A, 0x0F, 0xAA, 4,    NA,    CSYES},
	{"ALTERNATIVE WORLD G."	,MSbF, 0x4A, 0x33, NA,   0x65, 0x01, 0x00, 192,  NA,    CSNO},
	{"RAINBOW ARTS F1"	,LSbF, 0x2A, 0x19, NA,   0x36, 0xA0, 0x0A, 800,  NA,    CSYES},
	{"RAINBOW ARTS F2"	,LSbF, 0x2A, 0x19, NA,   0x36, 0xA0, 0x0A, 800,  NA,    CSYES},
	{"TRILOGIC"		,MSbF, 0x28, 0x1C, NA,   0x35, 0x0F, 0x0E, 200,  NA,    CSYES},

	/* name,                 en,   tp,   sp,   mp,   lp,   pv,   sv,   pmin, pmax,  has_cs. */

	{"BURNER VARIANT"	,VV,   0x30, 0x23, NA,   0x42, VV,   VV,   50,   NA,    CSNO},
	{"OCEAN NEW TAPE F4"	,MSbF, 0x2D, 0x24, NA,   0x44, 0x40, 0x5A, 50,   200,   CSYES},
	{"TDI TAPE F2"		,LSbF, NA,   0x44, NA,   0x65, 0xA0, 0x0A, 50,   NA,    CSYES},
	{"BITURBO"		,MSbF, 0x21, 0x1B, NA,   0x27, 0x02, 0x10, 400,  NA,    CSYES},
	{"108DE0A5"		,LSbF, 0x1F, 0x1B, NA,   0x30, 0x02, 0x09, 200,  NA,    CSYES},
	{"ACTIONREPLAY_HEADER"  ,LSbF, 0x3A, 0x23, NA,   0x53, 1,    0,    1500, NA,    CSNO},
	{"ACTIONREPLAY_TURBO"   ,LSbF, 0x3A, 0x23, NA,   0x53, NA,   NA,   NA,   NA,    CSYES},
	{"ACTIONREPLAY_SUPERTURBO"
				,LSbF, 0x22, 0x13, NA,   0x2B, NA,   NA,   NA,   NA,    CSYES},
	{"ASH AND DAVE"		,MSbF, 0x2D, 0x22, NA,   0x44, 0x80, 0x40, 100,  NA,    CSNO},
	{"FREELOAD SLOWLOAD T1"	,MSbF, 0x77, 0x5A, NA,   0x85, 0x40, 0x5A, 45,   400,   CSYES},
	{"FREELOAD SLOWLOAD T2"	,MSbF, 0x9A, 0x66, NA,   0xCD, 0x40, 0x5A, 45,   400,   CSYES},
	{"GO FOR THE GOLD"	,LSbF, 0x2F, 0x1D, NA,   0x42, 0x02, 0x11, 200,  NA,    CSYES},
	{"JIFFY LOAD T1"	,MSbF, 0x1D, 0x17, NA,   0x21, 0x10, 0x20, 200,  NA,    CSNO},
	{"JIFFY LOAD T2"	,MSbF, 0x27, 0x1F, NA,   0x30, 0x10, 0x20, 200,  NA,    CSNO},
	{"FREEZE FRAME TAPE"	,LSbF, 0x34, 0x28, NA,   0x3F, 0,    1,    1500, NA,    CSNO},
	{"TES TAPE"		,LSbF, 0x30, 0x1D, NA,   0x44, 0x02, 0x52, 100,  NA,    CSYES},
	{"TEQUILA SUNRISE"	,MSbF, 0x22, 0x1A, NA,   0x28, 0x02, 0x09, 50,   NA,    CSNO},
	{"GRAPHIC ADV. CREATOR"	,LSbF, NA,   0x3D, 0x52, 0x7E, 1,    0,    2000, NA,    CSNO},
	{"CHUCKIE EGG"		,MSbF, NA,   0x28, NA,   0x44, 0xFF, 0x00, 25,   NA,    CSYES},
	{"ALTERNATIVE SW DK T1"	,MSbF, NA,   0x2B, 0x64, 0xB5, 0,    1,    5,    NA,    CSYES},
	{"ALTERNATIVE SW DK T2"	,MSbF, NA,   0x21, 0x36, 0xA5, 0,    1,    5,    NA,    CSYES},
//	{"ALTERNATIVE SW DK T3"	,MSbF, NA,   0x44, 0x88, 0xAF, 0,    1,    5,    NA,    CSYES},
	{"ALTERNATIVE SW DK T3"	,MSbF, NA,   0x3F, 0x86, 0xAB, 0,    1,    5,    NA,    CSYES},
	{"ALTERNATIVE SW DK T4"	,MSbF, NA,   0x23, 0x52, 0xAB, 0,    1,    5,    NA,    CSYES},
	{"POWER LOAD"		,MSbF, 0x20, 0x1C, NA,   0x29, 0x02, 0x09, 400,  NA,    CSYES},
	{"GREMLIN F1"		,LSbF, 0x30, 0x22, NA,   0x41, 0xE3, 0xED, 64,   NA,    CSYES},
	{"GREMLIN F2"		,LSbF, 0x2C, 0x1E, NA,   0x3C, 0xE3, 0xED, 64,   NA,    CSYES},
	{"AMERICAN ACTION"	,MSbF, 0x20, 0x1A, NA,   0x28, 0x02, 0x09, 64,   NA,    CSNO},
	/*
	 * Creatures loader uses inverted bit pulses hence pv and sv are real values ^ 0xFF.
	 * Also, the loader does not use checksums but these were calculated using a healthy
	 * version (to the best of our knowledge).
	 */
	{"CREATURES"		,MSbF, 0x3A, 0x2E, NA,   0x4C, 0xF0, 0x47, 64,   NA,    CSYES},
	{"RAINBOW ISLANDS"	,MSbF, 0x2C, 0x24, NA,   0x42, 0x40, 0x5A, 45,   400,   CSYES},
	{"OCEAN NEW TAPE F3"	,MSbF, NA,   0x2E, 0x49, 0x80, 1,    0,    32,   NA,    CSYES},
	{"EASY-TAPE"		,LSbF, 0x30, 0x1D, NA,   0x40, 0x02, 0x52, 200,  NA,    CSYES},
	{"TURBO 220"		,MSbF, 0x20, 0x1A, NA,   0x28, 0x02, 0x09, 64,   NA,    CSNO},
	{"CREATIVE SPARKS"	,MSbF, 0x2A, 0x22, NA,   0x33, 0x01, 0xFF, 64,   NA,    CSYES},
	{"DIGITAL DESIGN"	,MSbF, 0x2D, 0x20, NA,   0x42, 0x80, 0x40, 100,  NA,    CSNO},
	{"GLASS TAPE HEADER"	,MSbF, 0x58, 0x3F, NA,   0x7D, 1,    0,    0x800,NA,    CSYES},
	{"GLASS TAPE DATA"	,MSbF, 0x58, 0x3F, NA,   0x7D, 1,    0,    0x300,NA,    CSYES},
	{"TURBOTAPE 526 HEADER"	,MSbF, 0x42, 0x31, NA,   0x4B, 0x02, 0x09, 0x100,NA,    CSNO},
	{"TURBOTAPE 526 DATA"	,MSbF, 0x42, 0x31, NA,   0x4B, 0x02, 0x09, 0x80, NA,    CSYES},
	{"MICROLOAD VARIANT T1"	,LSbF, 0x2A, 0x1D, NA,   0x33, 0xA5, 0x0A, 64,   NA,    CSYES},
	{"MICROLOAD VARIANT T2"	,LSbF, 0x34, 0x2A, NA,   0x42, 0xA5, 0x0A, 64,   NA,    CSYES},
	{"LEXPEED"		,MSbF, 0x5E, 0x4C, NA,   0x72, 0x02, 0x09, 1000, NA,    CSYES},
	{"MMS"			,MSbF, 0x21, 0x1B, NA,   0x28, 0x02, 0x07, 375,  NA,    CSYES},
	{"GREMLIN GBH HEADER"	,MSbF, NA,   0x2F, 0x49, 0x80, 1,    0,    512,  NA,    CSNO},
	{"GREMLIN GBH DATA"	,MSbF, NA,   0x2F, 0x49, 0x80, 1,    0,    512,  NA,    CSNO},
	{"LK AVALON"		,MSbF, 0x21, 0x1A, NA,   0x28, 0x02, 0x09, 50,   NA,    CSYES},
	{"TURBOTAPE 263 HEADER"	,LSbF, 0x20, 0x1B, NA,   0x27, 0x02, 0x09, 0x100,NA,    CSNO},
	{"TURBOTAPE 263 DATA"	,LSbF, 0x20, 0x1B, NA,   0x27, 0x02, 0x09, 0x80, NA,    CSYES},
	{"GYROSPEED"		,MSbF, 0x1F, 0x15, NA,   0x2A, 0x40, 0x5A, 45,   400,   CSYES},

	/* name,                 en,   tp,   sp,   mp,   lp,   pv,   sv,   pmin, pmax,  has_cs. */

	{"MSX TAPE HEADER"	,LSbF, NA,   0x30, NA,   0x60, 1,    0,    6000, NA,    CSNO},
	{"MSX TAPE DATA"	,LSbF, NA,   0x30, NA,   0x60, 1,    0,    1000, NA,    CSNO},
	{"MSX TAPE HEADER FAST"	,LSbF, NA,   0x18, NA,   0x30, 1,    0,    6000, NA,    CSNO},
	{"MSX TAPE DATA FAST"	,LSbF, NA,   0x18, NA,   0x30, 1,    0,    1000, NA,    CSNO},
	{"TURBOTAPE-64-FAST HEADER",MSbF, 0x0E, 0x0B, NA, 0x12, 0x02, 0x09, 50,  NA,    CSNO},
	{"TURBOTAPE-64-FAST DATA"  ,MSbF, 0x0E, 0x0B, NA, 0x12, 0x02, 0x09, 50,  NA,    CSYES},

	/* Closing record (mandatory cap used e.g. in persistence module for iteration) */
	{""			,666,  666,  666,  666,  666,  666,  666,  666,  666,   666}

	/* name,                 en,   tp,   sp,   mp,   lp,   pv,   sv,   pmin, pmax,  has_cs. */
};

#ifdef TAPCLEAN_EMBEDDED
struct fmt_t *ft;	/* PSRAM working copy, made by tapclean_init() */

const void *tapclean_ft_defaults(size_t *size)
{
	*size = sizeof(ft_defaults);
	return ft_defaults;
}
#endif

/*
 * The following strings are used to describe which loader signature has been
 * found in CBM data file.
 *
 * See enum for this table in mydefs.h: these must be kept in sync.
 */

const char knam[][48] = {
	/*
	 * Only loaders with a LID_ entry in mydefs.h enums. Do not list
	 * them all here!
	 */
	{"n/a"},
	{"Freeload (or clone)"},
	{"Odeload"},
	{"Bleepload"},
	{"Mega-Save"},
	{"Burner"},
	{"Wildload"},
	{"US Gold loader"},
	{"Microload"},
	{"Ace of Aces loader"},
	{"Turbotape 250"},
	{"Rack-It loader"},
	{"Ocean/Imagine (ns)"},
	{"Rasterload"},
	{"Super Pavloda"},
	{"Hit-Load"},
	{"Anirog loader (or clone)"},
	{"Visiload T1"},
	{"Visiload T2"},
	{"Visiload T3"},
	{"Visiload T4"},
	{"Visiload T5"},
	{"Visiload T6"},
	{"Visiload T7"},
	{"Firebird loader"},
	{"Novaload (ns)"},
	{"IK loader"},
	{"Pavloda"},
	{"Cyberload"},
	{"Virgin"},
	{"Hi-Tec"},
	{"Flashload"},
	{"Supertape"},
	{"Ocean New 1 T1"},
	{"Ocean New 1 T2"},
	{"Atlantis loader"},
	{"Snakeload"},
	{"Ocean New 2"},
	{"Audiogenic"},
	{"Freeze Machine tape"},
	{"Accolade (or clone)"},
	{"Rainbow Arts (F1/F2)"},
	{"Burner (Mastertronic Variant)"},
	{"Ocean New 4"},
	{"108DE0A5"},
	{"Freeload Slowload"},
	{"Go For The Gold"},
	{"Jiffy Load"},
	{"Freeze Frame Tape"},
	{"TES Tape"},
	{"Tequila Sunrise"},
	{"GAC Save tape"},
	{"Chuckie Egg"},
	{"Alternative SW (DK)"},
	{"Power Load"},
	{"Gremlin (F1/F2)"},
	{"Easy-Tape System C"},
	{"Creative Sparks"},
	{"Trilogic"},
	{"Glass Tape"},
	{"Microload (Blue Ribbon Variant)"},
	{"Lexpeed Fastsave System"},
	{"MMS Tape"},
	{"Gremlin GBH"},
	{"Gyrospeed"},
	{"Turbotape 64 Fast"},
	/*
	 * Only loaders with a LID_ entry in mydefs.h enums. Do not list
	 * them all here!
	 */
};


char exedir[MAXPATH];			/* global, assigned in main(), includes trailing slash. */
char reportdir[MAXPATH];		/* global, assigned in main(). */


/* note: all generated files are saved to reportdir */

static const char tcreportname[] =	"tcreport.txt";
static const char temptcreportname[] =	"tcreport.tmp";
static const char tcinfoname[] =	"tcinfo.txt";
static char cleanedtapname[MAXPATH];	/* assigned in main(). */

const char tcbatchreportname[] =	"tcbatch.txt";
const char temptcbatchreportname[] =	"tcbatch.tmp";


/*
 * Internal usage functions
 */

/*
 * Erase all stored data for the loaded 'tap', free buffers,
 * and reset database entries.
 */

static void unload_tap(void)
{
	int i;

	strcpy(tap.path, "");
	strcpy(tap.name, "");
	strcpy(tap.cbmname, "");

	if(tap.tmem != NULL) {
		free(tap.tmem);
		tap.tmem = NULL;
	}

	tap.len = 0;

	for(i = 0; i < 256; i++) {
		tap.pst[i] = 0;	/* Pulse stats table */
		tap.fst[i] = 0;	/* File  stats table */
	}

	tap.fsigcheck = 0;
	tap.fvercheck = 0;
	tap.fsizcheck = 0;
	tap.detected = 0;
	tap.detected_percent = 0;
	tap.purity = 0;
	tap.total_gaps = 0;
	tap.total_data_files = 0;
	tap.total_checksums = 0;
	tap.total_checksums_good = 0;
	tap.optimized_files = 0;
	tap.total_read_errors = 0;
	tap.fdate = 0;
	tap.taptime = 0;
	tap.version = 0;
	tap.bootable = 0;
	tap.changed = 0;
	tap.crc = 0;
	tap.cbmcrc = 0;
	tap.cbmdatalen = 0;
	tap.cbmhcrc = 0;
	tap.cbmid = LID_NONE;
	tap.tst_hd = 0;
	tap.tst_rc = 0;
	tap.tst_op = 0;
	tap.tst_cs = 0;
	tap.tst_rd = 0;

	database_reset_blk_db();
	database_reset_prg_db();
}

#ifndef TAPCLEAN_EMBEDDED
/*
 * Unallocate tap, crc_table and database
 */

static void cleanup_main(void)
{
	unload_tap();
	crc32_free_crc_table();

	/* deallocate ram from file database */
	database_destroy_blk_db();
}

/*
 * Get exe path from argv[0]...
 */

static int get_exedir(char *argv0)
{
	char *ret;

#ifdef WIN32
	/* First check if argv0 fits inside our buffer */
	if (strlen(argv0) > MAXPATH - 1)
		return FALSE;

	/* It's now safe to copy inside the buffer (padding with nulls!) */
	strncpy(exedir, argv0, MAXPATH);

	/* When run at the console argv[0] is simply "tapclean" or "tapclean.exe" */

	/* Note: we do this instead of using getcwd() because getcwd does not give
	 * the exe's directory when dragging and dropping a tap file to the program
	 * icon using windows explorer.
	 */
	if (stricmp(exedir, "tapclean") != 0 && stricmp(exedir, "tapclean.exe") != 0) {
		int i;

		/* Clip to leave path only */
		for (i = strlen(exedir) - 1; i > 0 && exedir[i] != SLASH; i--)
			;

		if (exedir[i] == SLASH)
			exedir[i + 1] = '\0';

		return TRUE;
	}
#endif

	ret = (char *) getcwd(exedir, MAXPATH - 2);
	if (ret == NULL)
		return FALSE;

	exedir[strlen(exedir)] = SLASH;
	exedir[strlen(exedir)] = '\0';

	return TRUE;
}

/*
 * Display usage (rationalised).
 */

static void display_usage(void)
{
	printf("\n");
	printf("Usage:\n");
	printf("tapclean [[option][parameter]] [[modifier][parameter]]...\n");
	printf("Example: tapclean -o giana_sisters.tap -tol 12\n");

	printf("\n");
	printf("Options:\n");
	printf(" -t   <tape>    Test tape image\n");
	printf(" -o   <tape>    Optimize tape image\n");
	printf(" -b   <dir>     Batch test tape images in <dir>\n");
	printf(" -rd  <dir>     Save report files to <dir>\n");
	printf(" -au  <tape>    Convert tape image to Sun AU audio file (44kHz)\n");
	printf(" -wav <tape>    Convert tape image to Microsoft WAV audio file (44kHz)\n");
	printf(" -rs  <tap>     Correct the 'size' field of a TAP file header\n");
	printf(" -ct0 <tape>    Convert tape image to version 0 TAP format\n");
	printf(" -ct1 <tape>    Convert tape image to version 1 TAP format\n");

	printf("\n");
	printf("Modifiers:\n");
	printf(" -boostclean    Raise cleaning threshold\n");
	printf(" -debug         Allow detected files to overlap\n");
	printf(" -do<loader>    Scan only for <loader>\n");
	printf(" -docyberfault  Report Cyberload F3 bad checksums of $04\n");
	printf(" -doprg         Create PRG files\n");
	printf(" -extvisipatch  Extract Visiload loader patch files\n");
	printf(" -fstats        Pulse stats are per file\n");
	printf(" -incsubdirs    Make batch scan include subdirectories\n");
	printf(" -list          List of supported scanners and options used by -no<loader>\n");
	printf(" -no<loader>    Don't scan for <loader> Example: -nocyber\n");
	printf(" -noaddpause    Don't add a pause to the file end after clean\n");
	printf(" -noc64eof      C64 ROM scanner will not expect EOF markers\n");
	printf(" -noid          Disable scanning for only the 1st ID'd loader\n");
	printf(" -preserve      Preserve loader variables between program executions\n");
	printf(" -prgunite      Connect neighbouring PRG's into a single file\n");
	printf(" -reckless      Allow cleaning of tape images with errors\n");
	printf(" -sine          Make audio converter use sine waves\n");
	printf(" -skewadapt     Use skewed pulse adapting bit reader\n");
	printf(" -sortbycrc     Batch scan sorts report by cbmcrc values\n");
	printf(" -tol <0-15>    Set pulsewidth read tolerance, default = 10\n");

	/*
	 * These switches should only be used for legacy TAP/DMP files produced
	 * as per below:
	 *
	 * VIC20 (-20): MTAP < 0.26 (PAL), MTAP < 0.27 (NTSC), dc2nconv < 1.4
	 * C16   (-16): MTAP < 0.30, dc2nconv < 1.4
	 *
	 * Additionally, these switches can be used in case of a TAP/DMP file
	 * whose contents are intended for more than one platform.
	 */
	printf("\n");
	printf("Experimental modifiers (for advanced users):\n");
	printf(" -16            Force Commodore 16 tape\n");
	printf(" -20            Force Commodore VIC 20 tape\n");
	printf(" -64            Force Commodore 64 tape (default)\n");
	printf(" -ntsc          NTSC timing\n");
	printf(" -pal           PAL timing (default)\n");
}

/*
 * Display scanner list (rationalised).
 */

static void display_scanners(void)
{
	int i, sl;	/* Counter and amount of loader switches */

	printf("\nList of supported scanners and their -no<loader>/-do<loader> parameter names:\n\n");

	sl = sizeof(ldrswt) / sizeof(*ldrswt);
	for (i = 0; i < sl; i++) {
		printf(" %-24s  -no%-12s  -do%-12s\n",
			ldrswt[i].desc,
			ldrswt[i].par,
			ldrswt[i].par
			);
	}
}

/*
 * Exclude all loaders but the CBM ROM one (rationalised).
 */

static void exclude_all_but_cbm(void)
{
	int i, sl;	/* Counter and amount of loader switches */

	ldrswt[0].exclude = FALSE;	/* Never exclude CBM ROM Loader */

	sl = sizeof(ldrswt) / sizeof(*ldrswt);
	for (i = 1; i < sl; i++)
		ldrswt[i].exclude = TRUE;
}

/*
 * Process options
 */

static void process_options(int argc, char **argv)
{
	int i;
	int excludeflag = 1;
	int jj, sl;	/* counter and amount of loader switches */

	sl = sizeof(ldrswt) / sizeof(*ldrswt);

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-tol") == 0) {		/* flag = set tolerance */
			if (argv[i + 1] != NULL) {
				tol = atoi(argv[i + 1]) + 1;	/* 1 = zero tolerance (-tol 0) */
				if (tol < 1 || tol > MAXTOL) {
					tol = DEFTOL;
					printf("\n\nTolerance parameter out of range, using default (= 10).");
				}
			} else {
				printf("\n\nTolerance parameter missing, using default (= %d).", DEFTOL - 1);
			}
		}

		if (strcmp(argv[i], "-rd") == 0) {		/* flag = set directory for report files */
			if (argv[i + 1] != NULL) {
				if (strlen(argv[i + 1]) > MAXPATH - 1)
					printf("\n\nDirectory location for report files is too long.");
				else
					strcpy(reportdir, argv[i + 1]);
			} else {
				printf("\n\nMissing directory location for report files, using current.");
			}
		}

		if (strcmp(argv[i], "-debug") == 0)
			debug = TRUE;
		if (strcmp(argv[i], "-noid") == 0)
			noid = TRUE;
		if (strcmp(argv[i], "-16") == 0) {
			c16 = TRUE;
			c64 = FALSE;
		}
		if (strcmp(argv[i], "-20") == 0) {
			c20 = TRUE;
			c64 = FALSE;
		}
		if (strcmp(argv[i], "-64") == 0)
			c64 = TRUE;
		if (strcmp(argv[i], "-pal") == 0)
			pal = TRUE;
		if (strcmp(argv[i], "-ntsc") == 0)
			ntsc = TRUE;
		if (strcmp(argv[i], "-noc64eof") == 0)
			noc64eof = TRUE;
		if (strcmp(argv[i], "-docyberfault") == 0)
			docyberfault = TRUE;
		if (strcmp(argv[i], "-boostclean") == 0)
			boostclean = TRUE;
		if (strcmp(argv[i], "-noaddpause") == 0)
			noaddpause = TRUE;
		if (strcmp(argv[i], "-reckless") == 0)
			reckless = TRUE;
		if (strcmp(argv[i], "-sine") == 0)
			sine = TRUE;
		if (strcmp(argv[i], "-preserve") == 0)
			preserveloadervars = TRUE;
		if (strcmp(argv[i], "-prgunite") == 0) {
			prgunite = TRUE;
			doprg = TRUE;
		}
		if (strcmp(argv[i], "-doprg") == 0)
			doprg = TRUE;
		if (strcmp(argv[i], "-extvisipatch") == 0)
			extvisipatch = TRUE;
		if (strcmp(argv[i], "-incsubdirs") == 0)
			incsubdirs = TRUE;
		if (strcmp(argv[i], "-list") == 0) {
			display_scanners();
			exit(0);
		}
		if (strcmp(argv[i], "-sortbycrc") == 0)
			sortbycrc = TRUE;
		if (strcmp(argv[i], "-ec") == 0)
			exportcyberloaders = TRUE;
		if (strcmp(argv[i], "-skewadapt") == 0)
			skewadapt = TRUE;
		if (strcmp(argv[i], "-fstats") == 0)
			fstats = TRUE;

		/* process all -no<loader> */
		if (strncmp(argv[i], "-no", 3) == 0) {
			for(jj = 0; jj < sl; jj++) {
				if (strcmp(argv[i]+3, ldrswt[jj].par) ==  0) {
					if (excludeflag == 1) {
						printf("\nExcluded scanners:\n\n");
						/* Print above message only once */
						excludeflag = 0;
					} else if (excludeflag == 2) {
						printf("You cannot mix -no<loader> and -do<loader>\n");
						exit(1);
					}
					ldrswt[jj].exclude = TRUE;
					printf(" %s\n", ldrswt[jj].desc);
				}
			}
		}


		/* process all -do<loader> */

		if (strncmp(argv[i], "-do", 3) == 0) {
			for(jj = 0; jj < sl; jj++) {
				if (strcmp(argv[i]+3, ldrswt[jj].par) ==  0) {
					if (excludeflag == 1) {
						printf("\nIncluded scanners:\n\n");
						exclude_all_but_cbm();
						/* Print above message only once */
						excludeflag = 2;
					} else if (excludeflag == 0) {
						printf("You cannot mix -no<loader> and -do<loader>\n");
						exit(1);
					}
					ldrswt[jj].exclude = FALSE;
					printf(" +%s\n", ldrswt[jj].desc);
				}
			}
		}
	}

	printf("\nRead tolerance = %d", tol - 1);
}

/*
 * Choose CPU cycles based on computer type and PAL/NTSC
 *
 * (Embedded build: tapclean_load_buffer() sets 'cps' directly.)
 */

static void handle_cps(void)
{
	printf("\n\nComputer type: ");

	if (c64 == TRUE) {
		printf("C64 ");
		if (pal == TRUE)
			cps = C64_PAL_CPS;
		else
			cps = C64_NTSC_CPS;
	} else if (c16 == TRUE) {
		printf("C16 ");
		/* Force CBM pulsewidths to the ones found in C16/+4 tapes */

		ft [CBM_HEAD].sp = 0x35;
		ft [CBM_HEAD].mp = 0x6A;
		ft [CBM_HEAD].lp = 0xD4;

		ft [CBM_DATA].sp = 0x35;
		ft [CBM_DATA].mp = 0x6A;
		ft [CBM_DATA].lp = 0xD4;

		if (pal == TRUE)
			cps = C16_PAL_CPS;
		else
			cps = C16_NTSC_CPS;
	} else if (c20 == TRUE) {
		printf("VIC20 ");

		/* Force CBM pulsewidths to the ones found in VIC20 tapes */
		ft [CBM_HEAD].sp = 0x2B;
		ft [CBM_HEAD].mp = 0x3F;
		ft [CBM_HEAD].lp = 0x53;

		ft [CBM_DATA].sp = 0x2B;
		ft [CBM_DATA].mp = 0x3F;
		ft [CBM_DATA].lp = 0x53;

		if (pal == TRUE)
			cps = VIC20_PAL_CPS;
		else
			cps = VIC20_NTSC_CPS;
	}

	if (pal == TRUE)
		printf("PAL ");
	else
		printf("NTSC ");

	printf("(%ld Hz)\n", cps);
}
#endif /* !TAPCLEAN_EMBEDDED */

/*
 * Search the tap for all known (and enabled) file formats.
 *
 * Results are stored in the 'blk' database (an array of structs).
 *
 * If global variable 'quiet' is set (1), the scanners will not print a
 * "Scanning: xxxxxxx" string. This simply reduces text output and I prefer this
 * when optimizing (Search is called quite frequently).
 *
 * Note: This function calls all 'loadername_search' routines and as a result it
 *       fills out only about half of the fields in the blk[] database, they are...
 *
 *       lt, p1, p2, p3, p4, xi (ie, all fields supported by 'database_add_blk_def()').
 *
 *       the remaining fields (below) are filled out by 'describe_blocks()' which
 *       calls all 'loadername_decribe()' routines for the particular format (lt).
 *
 *       cs, ce, cx, rd_err, crc, cs_exp, cs_act, pilot_len, trail_len, ok.
 */

static void search_tap(void)
{
	long i;

	database_is_full = FALSE;	/* enable the "database full" warning. */
				/* note: database_add_blk_def sets it 1 when full. */

	msgout("\nScanning...");

	if (tap.changed) {
		database_reset_blk_db();

		/* initialize the read error table */

		for (i = 0; i < NUM_READ_ERRORS; i++)
			read_errors[i] = 0;


		/* CALL THE SCANNERS!... */

		pause_search();		/* pauses and CBM files always get searched for...  */
		cbm_search();

		/* Note : if cbm parts are found then this call (cbm_search) will create copies
		 * of the header and data parts in 'cbm_header[]' and 'cbm_program[]'.
		 * Also a crc32 of the data block is recorded in 'tap.cbmcrc'.
		 */

		/* try and id any loader stored in cbm_header[] or cbm_program[]... */

		tap.cbmid = idloader(tap.cbmcrc, tap.cbmdatalen);

		if (!quiet) {
			sprintf(lin, "\n  Loader ID: %s.\n", knam[tap.cbmid]);
			msgout(lin);
		}

		if (noid == FALSE) {	/* scanning shortcuts enabled?  */
			if (tap.cbmid == LID_T250 	&& ldrswt[noturbo].exclude == FALSE && !database_is_full && !aborted)
				turbotape_search();

			if (tap.cbmid == LID_TTFAST	&& ldrswt[noturbofast].exclude == FALSE && !database_is_full && !aborted)
				turbotape_fast_search();

			if (tap.cbmid == LID_FREE 	&& ldrswt[nofree ].exclude== FALSE && !database_is_full && !aborted)
				freeload_search();

			if (tap.cbmid == LID_ODE 	&& ldrswt[noode ].exclude== FALSE && !database_is_full && !aborted)
				odeload_search();

			if (tap.cbmid == LID_NOVA 	&& ldrswt[nonova ].exclude== FALSE && !database_is_full && !aborted) {
				nova_spc_search();
				nova_search();
			}

			if (tap.cbmid == LID_BLEEP 	&& ldrswt[nobleep].exclude == FALSE && !database_is_full && !aborted) {
				bleep_search();
				bleep_spc_search();
			}

			if (tap.cbmid == LID_OCEAN 	&& ldrswt[noocean].exclude == FALSE && !database_is_full && !aborted)
				ocean_search();

			if (tap.cbmid == LID_CYBER 	&& ldrswt[nocyber].exclude == FALSE &&!database_is_full) {
				cyberload_f1_search();
				cyberload_f2_search();
				cyberload_f3_search();
				cyberload_f4_search();
			}

			if (tap.cbmid == LID_USG 	&& ldrswt[nousgold	].exclude == FALSE && !database_is_full && !aborted)
				usgold_search();

			if (tap.cbmid == LID_ACE 	&& ldrswt[noaces 	].exclude == FALSE && !database_is_full && !aborted)
				aces_search();

			if (tap.cbmid == LID_MIC 	&& ldrswt[nomicro	].exclude == FALSE && !database_is_full && !aborted)
				micro_search();

			if (tap.cbmid == LID_MICVAR 	&& ldrswt[nomicrovar	].exclude == FALSE && !database_is_full && !aborted)
				microloadvar_search(0);	/* All types */

			if (tap.cbmid == LID_RAST 	&& ldrswt[noraster	].exclude == FALSE && !database_is_full && !aborted)
				raster_search();

			if (tap.cbmid == LID_MEGASAVE 	&& ldrswt[nomegasave	].exclude == FALSE && !database_is_full)
				megasave_search(0);	/* All types */

			if (tap.cbmid == LID_BURN 	&& ldrswt[noburner 	].exclude == FALSE && !database_is_full && !aborted)
				burner_search();

			if (tap.cbmid == LID_VIS_T1	&& ldrswt[novisi	].exclude == FALSE && !database_is_full && !aborted)
				visiload_search();

			if (tap.cbmid == LID_VIS_T2	&& ldrswt[novisi	].exclude == FALSE && !database_is_full && !aborted)
				visiload_search();

			if (tap.cbmid == LID_VIS_T3	&& ldrswt[novisi	].exclude == FALSE && !database_is_full && !aborted)
				visiload_search();

			if (tap.cbmid == LID_VIS_T4	&& ldrswt[novisi	].exclude == FALSE && !database_is_full && !aborted)
				visiload_search();

			if (tap.cbmid == LID_VIS_T5	&& ldrswt[novisi	].exclude == FALSE && !database_is_full && !aborted)
				visiload_search();

			if (tap.cbmid == LID_VIS_T6	&& ldrswt[novisi	].exclude == FALSE && !database_is_full && !aborted)
				visiload_search();

			if (tap.cbmid == LID_VIS_T7	&& ldrswt[novisi	].exclude == FALSE && !database_is_full && !aborted)
				visiload_search();

			if (tap.cbmid == LID_WILD 	&& ldrswt[nowild	].exclude == FALSE && !database_is_full && !aborted)
				wild_search();

			if (tap.cbmid == LID_HIT 	&& ldrswt[nohit		].exclude == FALSE && !database_is_full && !aborted)
				hitload_search();

			if (tap.cbmid == LID_RACK 	&& ldrswt[norackit	].exclude == FALSE && !database_is_full && !aborted)
				rackit_search();

			if (tap.cbmid == LID_SPAV 	&& ldrswt[nospav	].exclude == FALSE && !database_is_full && !aborted)
				superpav_search();

			if (tap.cbmid == LID_ANI 	&& ldrswt[noanirog	].exclude == FALSE && !database_is_full && !aborted) {
				anirog_search();
				if (ldrswt[nofree	].exclude == FALSE && !database_is_full && !aborted)
					freeload_search();
			}

			if (tap.cbmid == LID_SUPER 	&& ldrswt[nosuper	].exclude == FALSE && !database_is_full && !aborted)
				supertape_search();

			if (tap.cbmid == LID_FIRE 	&& ldrswt[nofire	].exclude == FALSE && !database_is_full && !aborted)
				firebird_search();

			if (tap.cbmid == LID_PAV 	&& ldrswt[nopav		].exclude == FALSE && !database_is_full && !aborted)
				pav_search();

			if (tap.cbmid == LID_IK 	&& ldrswt[noik		].exclude == FALSE && !database_is_full && !aborted)
				ik_search();

			if (tap.cbmid == LID_FLASH 	&& ldrswt[noflash	].exclude == FALSE && !database_is_full && !aborted)
				flashload_search();

			if (tap.cbmid == LID_VIRG 	&& ldrswt[novirgin	].exclude == FALSE && !database_is_full && !aborted)
				virgin_search();

			if (tap.cbmid == LID_HTEC 	&& ldrswt[nohitec	].exclude == FALSE && !database_is_full && !aborted)
				hitec_search();

			if (tap.cbmid == LID_OCNEW1_T1	&& ldrswt[nooceannew1	].exclude == FALSE && !database_is_full && !aborted)
				oceannew1_search(tap.cbmid);

			if (tap.cbmid == LID_OCNEW1_T2	&& ldrswt[nooceannew1	].exclude == FALSE && !database_is_full && !aborted)
				oceannew1_search(tap.cbmid);

			if (tap.cbmid == LID_OCNEW2	&& ldrswt[nooceannew2	].exclude == FALSE && !database_is_full && !aborted)
				oceannew2_search();

			if (tap.cbmid == LID_SNAKE	&& ldrswt[nosnake50	].exclude == FALSE && !database_is_full && !aborted)
				snakeload50_search(0);	/* T1/T2 */

			if (tap.cbmid == LID_SNAKE	&& ldrswt[nosnake51	].exclude == FALSE && !database_is_full && !aborted)
				snakeload51_search();

			if (tap.cbmid == LID_ATLAN	&& ldrswt[noatlantis	].exclude == FALSE && !database_is_full && !aborted)
				atlantis_search();

			if (tap.cbmid == LID_AUDIOGENIC	&& ldrswt[noaudiogenic	].exclude == FALSE && !database_is_full && !aborted)
				audiogenic_search();

			if (tap.cbmid == LID_FRZMACHINE	&& ldrswt[nofrzmachine	].exclude == FALSE && !database_is_full && !aborted)
				freezemachine_search();

			if (tap.cbmid == LID_ACCOLADE	&& ldrswt[noaccolade	].exclude == FALSE && !database_is_full && !aborted)
				accolade_search();

			if (tap.cbmid == LID_RAINBOWARTS	&& ldrswt[norainbowf1	].exclude == FALSE && !database_is_full && !aborted)
				rainbowf1_search();

			if (tap.cbmid == LID_RAINBOWARTS	&& ldrswt[norainbowf2	].exclude == FALSE && !database_is_full && !aborted)
				rainbowf2_search();

			if (tap.cbmid == LID_BURNERVAR	&& ldrswt[noburnervar	].exclude == FALSE && !database_is_full && !aborted)
				burnervar_search();

			if (tap.cbmid == LID_OCNEW4	&& ldrswt[nooceannew4	].exclude == FALSE && !database_is_full && !aborted)
				oceannew4_search();

			if (tap.cbmid == LID_108DE0A5	&& ldrswt[no108DE0A5	].exclude == FALSE && !database_is_full && !aborted)
				t108DE0A5_search();

			if (tap.cbmid == LID_FREE_SLOW	&& ldrswt[nofrslow	].exclude == FALSE && !database_is_full && !aborted)
				freeslow_search(0);	/* T1/T2 */

			if (tap.cbmid == LID_GOFORGOLD	&& ldrswt[nogoforgold	].exclude == FALSE && !database_is_full && !aborted)
				goforgold_search();

			if (tap.cbmid == LID_JIFFYLOAD	&& ldrswt[nojiffyload	].exclude == FALSE && !database_is_full && !aborted)
				jiffyload_search(0);	/* T1/T2 */

			if (tap.cbmid == LID_FFTAPE	&& ldrswt[nofftape	].exclude == FALSE && !database_is_full && !aborted)
				fftape_search();

			if (tap.cbmid == LID_TESTAPE	&& ldrswt[notestape	].exclude == FALSE && !database_is_full && !aborted)
				testape_search();

			if (tap.cbmid == LID_TEQUILA	&& ldrswt[notequila	].exclude == FALSE && !database_is_full && !aborted)
				tequila_search();

			if (tap.cbmid == LID_GRADVCREATOR	&& ldrswt[nogradvcreator	].exclude == FALSE  && !database_is_full && !aborted)
				graphicadventurecreator_search();

			if (tap.cbmid == LID_CHUCKIEEGG	&& ldrswt[nochuckie	].exclude == FALSE && !database_is_full && !aborted)
				chuckieegg_search();

			if (tap.cbmid == LID_ALTERDK	&& ldrswt[noalterdk	].exclude == FALSE  && !database_is_full && !aborted)
				alternativedk_search(0);	/* T1-T4 */

			if (tap.cbmid == LID_POWERLOAD	&& ldrswt[nopower	].exclude == FALSE  && !database_is_full && !aborted)
				powerload_search();

			/* Keep the order of Gremlin scanners to F2 first and then F1 */
			if (tap.cbmid == LID_GREMLIN	&& ldrswt[nogremlinf2	].exclude == FALSE  && !database_is_full && !aborted)
				gremlin_f2_search();

			if (tap.cbmid == LID_GREMLIN	&& ldrswt[nogremlinf1	].exclude == FALSE  && !database_is_full && !aborted)
				gremlin_f1_search();

			if (tap.cbmid == LID_EASYTAPE	&& ldrswt[noeasytape	].exclude == FALSE && !database_is_full && !aborted)
				easytape_search();

			if (tap.cbmid == LID_CSPARKS	&& ldrswt[nocsparks	].exclude == FALSE && !database_is_full && !aborted)
				creativesparks_search();

			if (tap.cbmid == LID_TRILOGIC	&& ldrswt[notrilogic	].exclude == FALSE && !database_is_full && !aborted)
				trilogic_search();

			if (tap.cbmid == LID_GLASS	&& ldrswt[noglass	].exclude == FALSE && !database_is_full && !aborted)
				glass_search();

			if (tap.cbmid == LID_LEXPEED	&& ldrswt[nolexpeed	].exclude == FALSE && !database_is_full && !aborted)
				lexpeed_search();

			if (tap.cbmid == LID_MMS	&& ldrswt[nomms		].exclude == FALSE && !database_is_full && !aborted)
				mms_search();

			if (tap.cbmid == LID_GREMLINGBH	&& ldrswt[nogremlingbh	].exclude == FALSE && !database_is_full && !aborted)
				gremlin_gbh_search();

			if (tap.cbmid == LID_GYROSPEED	&& ldrswt[nogyrospeed	].exclude == FALSE && !database_is_full && !aborted)
				gyrospeed_search();

			/*
			 * todo : TURRICAN
			 * todo : SEUCK
			 * todo : JETLOAD
			 * todo : TENGEN
			 */
		}

		/* Scan the lot.. (if shortcuts are disabled or no loader ID was found) */

		if ((noid == FALSE && tap.cbmid == LID_NONE) || (noid == TRUE)) {
			if (ldrswt[noturbo	].exclude == FALSE && !database_is_full && !aborted)
				turbotape_search();

			if (ldrswt[noturbofast	].exclude == FALSE && !database_is_full && !aborted)
				turbotape_fast_search();

			if (ldrswt[norislands	].exclude == FALSE && !database_is_full && !aborted)
				rainbowislands_search();

			if (ldrswt[nofree	].exclude == FALSE && !database_is_full && !aborted)
				freeload_search();

			if (ldrswt[noode	].exclude == FALSE && !database_is_full && !aborted)
				odeload_search();

			if (ldrswt[nofrzmachine	].exclude == FALSE && !database_is_full && !aborted)
				freezemachine_search();

			/*
			 * Comes here to avoid ocean misdetections.
			 * Snakeload is a 'safer' scanner than ocean (the
			 * confidence level with which it is acknowledged is
			 * higher than ocean).
			 */

			if (ldrswt[nosnake50	].exclude == FALSE && !database_is_full && !aborted)
				snakeload50_search(0);	/* T1/T2 */

			if (ldrswt[nosnake51	].exclude == FALSE && !database_is_full && !aborted)
				snakeload51_search();

			if (ldrswt[nonova	].exclude == FALSE && !database_is_full && !aborted) {
				nova_spc_search();
				nova_search();
			}

			if (ldrswt[nobleep	].exclude == FALSE && !database_is_full && !aborted) {
				bleep_search();
				bleep_spc_search();
			}

			if (ldrswt[noocean	].exclude == FALSE && !database_is_full && !aborted)
				ocean_search();

			if (ldrswt[nocyber	].exclude == FALSE && !database_is_full && !aborted) {
				cyberload_f1_search();
				cyberload_f2_search();
				cyberload_f3_search();
				cyberload_f4_search();
			}

			if (ldrswt[nousgold	].exclude == FALSE && !database_is_full && !aborted)
				usgold_search();

			if (ldrswt[noaces	].exclude == FALSE && !database_is_full && !aborted)
				aces_search();

			if (ldrswt[nomicro	].exclude == FALSE && !database_is_full && !aborted)
				micro_search();

			if (ldrswt[nomicrovar	].exclude == FALSE && !database_is_full && !aborted)
				microloadvar_search(0);	/* All types */

			if (ldrswt[noraster	].exclude == FALSE && !database_is_full && !aborted)
				raster_search();

			if (ldrswt[nomegasave	].exclude == FALSE && !database_is_full && !aborted)
				megasave_search(0);	/* All types */

			if (ldrswt[noburner	].exclude == FALSE && !database_is_full && !aborted)
				burner_search();

			if (ldrswt[novisi	].exclude == FALSE && !database_is_full && !aborted)
				visiload_search();

			if (ldrswt[nowild	].exclude == FALSE && !database_is_full && !aborted)
				wild_search();

			if (ldrswt[nohit	].exclude == FALSE && !database_is_full && !aborted)
				hitload_search();

			if (ldrswt[norackit	].exclude == FALSE && !database_is_full && !aborted)
				rackit_search();

			if (ldrswt[nospav	].exclude == FALSE && !database_is_full && !aborted)
				superpav_search();

			if (ldrswt[noanirog	].exclude == FALSE && !database_is_full && !aborted)
				anirog_search();

			if (ldrswt[nosuper	].exclude == FALSE && !database_is_full && !aborted)
				supertape_search();

			if (ldrswt[nofire	].exclude == FALSE && !database_is_full && !aborted)
				firebird_search();

			if (ldrswt[nopav	].exclude == FALSE && !database_is_full && !aborted)
				pav_search();

			if (ldrswt[noik		].exclude == FALSE && !database_is_full && !aborted)
				ik_search();

			if (ldrswt[noturr	].exclude == FALSE && !database_is_full && !aborted)
				turrican_search();

			if (ldrswt[noseuck	].exclude == FALSE && !database_is_full && !aborted)
				seuck1_search();

			if (ldrswt[nojet	].exclude == FALSE && !database_is_full && !aborted)
				jetload_search();

			if (ldrswt[noflash	].exclude == FALSE && !database_is_full && !aborted)
				flashload_search();

			if (ldrswt[novirgin	].exclude == FALSE && !database_is_full && !aborted)
				virgin_search();

			if (ldrswt[nohitec	].exclude == FALSE && !database_is_full && !aborted)
				hitec_search();

			/* Check F2 first (Luigi) */
			if (ldrswt[notdif2	].exclude == FALSE && !database_is_full && !aborted)
				tdif2_search();

			if (ldrswt[notdif1	].exclude == FALSE && !database_is_full && !aborted)
				tdi_search();

			if (ldrswt[nooceannew1	].exclude == FALSE && !database_is_full && !aborted)
				oceannew1_search(0);	/* T1/T2 */

			if (ldrswt[nooceannew2	].exclude == FALSE && !database_is_full && !aborted)
				oceannew2_search();

			if (ldrswt[noatlantis	].exclude == FALSE && !database_is_full && !aborted)
				atlantis_search();

			if (ldrswt[nopalacef1	].exclude == FALSE && !database_is_full && !aborted)
				palacef1_search();

			if (ldrswt[nopalacef2	].exclude == FALSE && !database_is_full && !aborted)
				palacef2_search();

			if (ldrswt[noenigma	].exclude == FALSE && !database_is_full && !aborted)
				enigma_search();

			if (ldrswt[noaudiogenic	].exclude == FALSE && !database_is_full && !aborted)
				audiogenic_search();

			if (ldrswt[noaliensy	].exclude == FALSE && !database_is_full && !aborted)
				aliensyndrome_search();

			if (ldrswt[noaccolade	].exclude == FALSE && !database_is_full && !aborted)
				accolade_search();

			if (ldrswt[noalterwg	].exclude == FALSE && !database_is_full && !aborted)
				alternativewg_search();

			if (ldrswt[norainbowf1	].exclude == FALSE && !database_is_full && !aborted)
				rainbowf1_search();

			if (ldrswt[norainbowf2	].exclude == FALSE && !database_is_full && !aborted)
				rainbowf2_search();

			if (ldrswt[noburnervar	].exclude == FALSE && !database_is_full && !aborted)
				burnervar_search();

			if (ldrswt[nooceannew4	].exclude == FALSE && !database_is_full && !aborted)
				oceannew4_search();

			if (ldrswt[nobiturbo	].exclude == FALSE && !database_is_full && !aborted)
				biturbo_search();

			if (ldrswt[no108DE0A5	].exclude == FALSE && !database_is_full && !aborted)
				t108DE0A5_search();

			if (ldrswt[noar		].exclude == FALSE && !database_is_full && !aborted)
				ar_search();

			if (ldrswt[noashdave	].exclude == FALSE && !database_is_full && !aborted)
				ashdave_search();

			if (ldrswt[nofrslow	].exclude == FALSE && !database_is_full && !aborted)
				freeslow_search(0);	/* T1/T2 */

			/*
			 * Find a mechanism that enables the following scans just upon detecting
			 * the very few titles using these formats. We don't really want to slow
			 * down scanning due to rare formats, but we need to be able to understand
			 * where these are in use. Hence the scans are here for the time being.
			 */

			if (ldrswt[noamaction	].exclude == FALSE && !database_is_full && !aborted)
				amaction_search();

			if (ldrswt[nocreatures	].exclude == FALSE && !database_is_full && !aborted)
				creatures_search();

			// Enabled due to "Catalypse" (side 1/2)
			if (ldrswt[notestape	].exclude == FALSE && !database_is_full && !aborted)
				testape_search();

			if (ldrswt[nooceannew3	].exclude == FALSE && !database_is_full && !aborted)
				oceannew3_search();

			if (ldrswt[noddesign	].exclude == FALSE && !database_is_full && !aborted)
				ddesign_search();

			if (ldrswt[noglass	].exclude == FALSE && !database_is_full && !aborted)
				glass_search();

			if (ldrswt[noturbo526	].exclude == FALSE && !database_is_full && !aborted)
				turbotape526_search();

			if (ldrswt[nolexpeed	].exclude == FALSE && !database_is_full && !aborted)
				lexpeed_search();

			if (ldrswt[nomms	].exclude == FALSE && !database_is_full && !aborted)
				mms_search();

			if (ldrswt[nogremlingbh	].exclude == FALSE && !database_is_full && !aborted)
				gremlin_gbh_search();

			if (ldrswt[noturbo263	].exclude == FALSE && !database_is_full && !aborted)
				turbotape263_search();

			if (ldrswt[nomsx	].exclude == FALSE && !database_is_full && !aborted)
				msx_search(0);	/* Standard/Fast */

			/*
			 * Find a mechanism that enables the following scans just upon detecting
			 * the very few titles using these formats. The problem is that detection
			 * is based on the CBM boot where these formats are usually used in tape
			 * compilations.
			 */

			if (ldrswt[noeasytape	].exclude == FALSE && !database_is_full && !aborted)
				easytape_search();

			if (ldrswt[noturbo220	].exclude == FALSE && !database_is_full && !aborted)
				turbo220_search();

			if (ldrswt[nolkavalon	].exclude == FALSE && !database_is_full && !aborted)
				lk_avalon_search();

			/*
			 * The REPEATed CBM Data file for Gyrospeed seems to be usually
			 * lacking EOF markers so we can't just rely on tap.cbmid. See TODO
			 * entry to move the calculation of tap.cbmid out of cbm_search().
			 */
			if (ldrswt[nogyrospeed	].exclude == FALSE && !database_is_full && !aborted)
				gyrospeed_search();

			/*
			 * Do not add the following ones because they should only be looked for when
			 * their signature is found in CBM Data block.
			 */

			//if (ldrswt[notrilogic	].exclude == FALSE && !database_is_full && !aborted)
			//	trilogic_search();

			//if (ldrswt[nofftape	].exclude == FALSE && !database_is_full && !aborted)
			//	fftape_search();

			//if (ldrswt[notequila	].exclude == FALSE && !database_is_full && !aborted)
			//	tequila_search();

			//if (ldrswt[gradvcreator	].exclude == FALSE  && !database_is_full && !aborted)
			//	graphicadventurecreator_search();

			//if (ldrswt[noalterdk	].exclude == FALSE  && !database_is_full && !aborted)
			//	alternativedk_search(0);	/* T1-T4 */

			//if (ldrswt[nopower	].exclude == FALSE  && !database_is_full && !aborted)
			//	powerload_search();

			//if (ldrswt[nogremlinf1].exclude == FALSE  && !database_is_full && !aborted)
			//	gremlin_f1_search();

			//if (ldrswt[nogremlinf2].exclude == FALSE  && !database_is_full && !aborted)
			//	gremlin_f2_search();

			//if (ldrswt[nocsparks	].exclude == FALSE && !database_is_full && !aborted)
			//	creativesparks_search();

			/*
			 * Do not add the following ones until additonal games using these formats
			 * are found.
			 * For the time being having these active would just slow down testing and
			 * cleaning other tapes. Besides, these are looked for when their signature
			 * is found in CBM Data block.
			 */

			//if (ldrswt[nogoforgold	].exclude == FALSE && !database_is_full && !aborted)
			//	goforgold_search();

			//if (ldrswt[nojiffyload	].exclude == FALSE && !database_is_full && !aborted)
			//	jiffyload_search(0);	/* T1/T2 */

			//if (ldrswt[nochuckie		].exclude == FALSE && !database_is_full && !aborted)
			//	chuckieegg_search();
		}

		database_sort_blks();	/* sort the blocks into order of appearance */
		database_scan_gaps();	/* add any gaps to the block list */
		database_sort_blks();	/* sort the blocks into order of appearance */

		tap.changed = 0;	/* important to clear this. */

		if (quiet)
			msgout("  Done.");
	} else {
		msgout("  Done (no changes).");
	}
}

/*
 * Write a description of a GAP file to global buffer 'info' for inclusion in
 * the report.
 */

static void gap_describe(int row)
{
	/* Gap size is stored within the "extra info" field of the record */
	if (blk[row]->xi > 1)
		sprintf(lin, "\n - Length = %d pulses", blk[row]->xi);
	else
		sprintf(lin, "\n - Length = %d pulse", blk[row]->xi);

	strcpy(info, lin);
}

/*
 * Pass this function a valid row number (i) from the file database (blk) and
 * it calls the describe function for that file format which will decode
 * any data in the block and fill in all missing information for that file.
 *
 * Note: Any "additional" (i.e. not stored in the block database) text info
 * will only be available in the global buffer 'info' (this could be improved).
 */

static void describe_file(int row)
{
	int t;

	t = blk[row]->lt;
	switch(t) {
		case GAP:		gap_describe(row);
					break;
		case PAUSE:		pause_describe(row);
					break;
		case CBM_HEAD:		cbm_describe(row);
					break;
		case CBM_DATA:		cbm_describe(row);
					break;
		case USGOLD:		usgold_describe(row);
					break;
		case TT_HEAD:		turbotape_describe(row);
					break;
		case TT_DATA:		turbotape_describe(row);
					break;
		case TTFAST_HEAD:	turbotape_fast_describe(row);
					break;
		case TTFAST_DATA:	turbotape_fast_describe(row);
					break;
		case FREE:		freeload_describe(row);
					break;
		case ODELOAD:		odeload_describe(row);
					break;
		case FREEZEMACHINE:	freezemachine_describe(row);
					break;
		case MEGASAVE_T1:	megasave_describe(row);
					break;
		case MEGASAVE_T2:	megasave_describe(row);
					break;
		case MEGASAVE_T3:	megasave_describe(row);
					break;
		case MEGASAVE_T4:	megasave_describe(row);
					break;
		case NOVA:		nova_describe(row);
					break;
		case NOVA_SPC:		nova_spc_describe(row);
					break;
		case WILD:		wild_describe(row);
					break;
		case WILD_STOP:		wild_describe(row);
					break;
		case ACES:		aces_describe(row);
					break;
		case OCEAN_F1:		ocean_describe(row);
					break;
		case OCEAN_F2:		ocean_describe(row);
					break;
		case OCEAN_F3:		ocean_describe(row);
					break;
		case RASTER:		raster_describe(row);
					break;
		case VISI_T1:		visiload_describe(row);
					break;
		case VISI_T2:		visiload_describe(row);
					break;
		case VISI_T3:		visiload_describe(row);
					break;
		case VISI_T4:		visiload_describe(row);
					break;
		case VISI_T5:		visiload_describe(row);
					break;
		case VISI_T6:		visiload_describe(row);
					break;
		case VISI_T7:		visiload_describe(row);
					break;
		case CYBER_F1:		cyberload_f1_describe(row);
					break;
		case CYBER_F2:		cyberload_f2_describe(row);
					break;
		case CYBER_F3:		cyberload_f3_describe(row);
					break;
		case CYBER_F4_1:	cyberload_f4_describe(row);
					break;
		case CYBER_F4_2:	cyberload_f4_describe(row);
					break;
		case CYBER_F4_3:	cyberload_f4_describe(row);
					break;
		case BLEEP:		bleep_describe(row);
					break;
		case BLEEP_TRIG:	bleep_describe(row);
					break;
		case BLEEP_SPC:		bleep_spc_describe(row);
					break;
		case HITLOAD:		hitload_describe(row);
					break;
		case MICROLOAD:		micro_describe(row);
					break;
		case MICROLOADVAR_T1:	microloadvar_describe(row);
					break;
		case MICROLOADVAR_T2:	microloadvar_describe(row);
					break;
		case BURNER:		burner_describe(row);
					break;
		case RACKIT:		rackit_describe(row);
					break;
		case SPAV1_HD:		superpav_describe(row);
					break;
		case SPAV2_HD:		superpav_describe(row);
					break;
		case SPAV1:		superpav_describe(row);
					break;
		case SPAV2:		superpav_describe(row);
					break;
		case VIRGIN:		virgin_describe(row);
					break;
		case HITEC:		hitec_describe(row);
					break;
		case ANIROG:		anirog_describe(row);
					break;
		case SUPERTAPE_HEAD:	supertape_describe(row);
					break;
		case SUPERTAPE_DATA:	supertape_describe(row);
					break;
		case PAV:		pav_describe(row);
					break;
		case IK:		ik_describe(row);
					break;
		case FBIRD_T1:		firebird_describe(row);
					break;
		case FBIRD_T2:		firebird_describe(row);
					break;
		case TURR_HEAD:		turrican_describe(row);
					break;
		case TURR_DATA:		turrican_describe(row);
					break;
		case SEUCK_L2:		seuck1_describe(row);
					break;
		case SEUCK_HEAD:	seuck1_describe(row);
					break;
		case SEUCK_DATA:	seuck1_describe(row);
					break;
		case SEUCK_TRIG:	seuck1_describe(row);
					break;
		case SEUCK_GAME:	seuck1_describe(row);
					break;
		case JET:		jetload_describe(row);
					break;
		case FLASH:		flashload_describe(row);
					break;
		case TDI_F1:		tdi_describe(row);
					break;
		case OCNEW1_T1:		oceannew1_describe(row);
					break;
		case OCNEW1_T2:		oceannew1_describe(row);
					break;
		case ATLAN:		atlantis_describe(row);
					break;
		case SNAKE51:		snakeload51_describe(row);
					break;
		case SNAKE50_T1:	snakeload50_describe(row);
					break;
		case SNAKE50_T2:	snakeload50_describe(row);
					break;
		case PAL_F1:		palacef1_describe(row);
					break;
		case PAL_F2:		palacef2_describe(row);
					break;
		case OCNEW2:		oceannew2_describe(row);
					break;
		case ENIGMA:		enigma_describe(row);
					break;
		case AUDIOGENIC:	audiogenic_describe(row);
					break;
		case ALIENSY:		aliensyndrome_describe(row);
					break;
		case ACCOLADE:		accolade_describe(row);
					break;
		case ALTERWG:		alternativewg_describe(row);
					break;
		case RAINBOWARTS_F1:	rainbowf1_describe(row);
					break;
		case RAINBOWARTS_F2:	rainbowf2_describe(row);
					break;
		case TRILOGIC:		trilogic_describe(row);
					break;
		case BURNERVAR:		burnervar_describe(row);
					break;
		case OCNEW4:		oceannew4_describe(row);
					break;
		case TDI_F2:		tdif2_describe(row);
					break;
		case BITURBO:		biturbo_describe(row);
					break;
		case T108DE0A5:		t108DE0A5_describe(row);
					break;
		case ACTIONREPLAY_HDR:	ar_describe_hdr(row);
					break;
		case ACTIONREPLAY_TURBO:
		case ACTIONREPLAY_STURBO:
					ar_describe_data(row);
					break;
		case ASHDAVE:		ashdave_describe(row);
					break;
		case FREE_SLOW_T1:	freeslow_describe(row);
					break;
		case FREE_SLOW_T2:	freeslow_describe(row);
					break;
		case GOFORGOLD:		goforgold_describe(row);
					break;
		case JIFFYLOAD_T1:	jiffyload_describe(row);
					break;
		case JIFFYLOAD_T2:	jiffyload_describe(row);
					break;
		case FFTAPE:		fftape_describe(row);
					break;
		case TESTAPE:		testape_describe(row);
					break;
		case TEQUILA:		tequila_describe(row);
					break;
		case GRADVCREATOR:	graphicadventurecreator_describe(row);
					break;
		case CHUCKIEEGG:	chuckieegg_describe(row);
					break;
		case ALTERDK_T1:	alternativedk_describe(row);
					break;
		case ALTERDK_T2:	alternativedk_describe(row);
					break;
		case ALTERDK_T3:	alternativedk_describe(row);
					break;
		case ALTERDK_T4:	alternativedk_describe(row);
					break;
		case POWERLOAD:		powerload_describe(row);
					break;
		case GREMLIN_F1:	gremlin_f1_describe(row);
					break;
		case GREMLIN_F2:	gremlin_f2_describe(row);
					break;
		case AMACTION:		amaction_describe(row);
					break;
		case CREATURES:		creatures_describe(row);
					break;
		case RAINBOW_ISLANDS:	rainbowislands_describe(row);
					break;
		case OCNEW3:		oceannew3_describe(row);
					break;
		case EASYTAPE:		easytape_describe(row);
					break;
		case TURBO220:		turbo220_describe(row);
					break;
		case CSPARKS:		creativesparks_describe(row);
					break;
		case DIGITAL_DESIGN:	ddesign_describe(row);
					break;
		case GLASS_HEAD:	glass_describe(row);
					break;
		case GLASS_DATA:	glass_describe(row);
					break;
		case TT526_HEAD:	turbotape526_describe(row);
					break;
		case TT526_DATA:	turbotape526_describe(row);
					break;
		case LEXPEED:		lexpeed_describe(row);
					break;
		case MMS:		mms_describe(row);
					break;
		case GREMLIN_GBH_HEAD:	gremlin_gbh_describe(row);
					break;
		case GREMLIN_GBH_DATA:	gremlin_gbh_describe(row);
					break;
		case LK_AVALON:		lk_avalon_describe(row);
					break;
		case TT263_HEAD:	turbotape263_describe(row);
					break;
		case TT263_DATA:	turbotape263_describe(row);
					break;
		case GYROSPEED:		gyrospeed_describe(row);
					break;
		case MSX_HEAD:		msx_describe(row);
					break;
		case MSX_DATA:		msx_describe(row);
					break;
		case MSX_HEAD_FAST:	msx_describe(row);
					break;
		case MSX_DATA_FAST:	msx_describe(row);
					break;
	}
}

/*
 * Describe each file in the database, this calls the loadername_describe()
 * function for the files type which fills in all missing information and
 * decodes each file.
 */

static void describe_blocks(void)
{
	int i, t, re;

	tap.total_read_errors = 0;

	for (i = 0; blk[i]->lt != LT_NONE; i++) {
		t = blk[i]->lt;

#ifdef TAPCLEAN_EMBEDDED
		/* The full-tap text report is never built in the embedded
		   build; reset 'info' per block so a small buffer suffices. */
		info[0] = '\0';
#endif
		describe_file(i);

		/* get generic info that all data blocks have... */

		if (t > PAUSE) {

			/* make crc32 if the block data has been extracted. */

			if (blk[i]->dd != NULL)
				blk[i]->crc = crc32_compute_crc(blk[i]->dd, blk[i]->cx);

			re = blk[i]->rd_err;
			tap.total_read_errors += re;
		}
	}
}

#ifndef TAPCLEAN_EMBEDDED
/*
 * Save buffer tap.tmem[] to a named file.
 * Return 1 on success, 0 on error.
 */

static int save_tap(char *name)
{
	FILE *fp;

	fp = fopen(name, "w+b");
	if (fp == NULL || tap.tmem == NULL)
		return 0;

	fwrite(tap.tmem, tap.len, 1, fp);
	fclose(fp);

	return 1;
}
#endif /* !TAPCLEAN_EMBEDDED */

/*
 * Look at the TAP header and verify signature as C64 TAP.
 * Return 0 if ok else 1 (rationalised).
 */

static int check_signature(void)
{
	if (strncmp((char *) tap.tmem, "C64-TAPE-RAW", 12) == 0)
		return 0;
	else
		return 1;
}

/*
 * Look at the TAP header and verify version, currently 0 and 1 are valid versions.
 * Sets 'version' variable and returns 0 on success, else returns 1.
 */

static int check_version(void)
{
	int b;

	b = tap.tmem[12];

	/* Only TAP v0 and v1 are supported */
	if (b == 0 || b == 1) {
		tap.version = b;
		return 0;
	} else {
		return 1;
	}
}

/*
 * Verify the TAP header 'size' field.
 * Returns 0 if ok, else 1.
 */

static int check_size(void)
{
	int sz;

	sz = (int) tap.tmem[16] + ((int) tap.tmem[17] << 8) +
		((int) tap.tmem[18] << 16) + ((int) tap.tmem[19] << 24);
	if (sz == tap.len - 20)
		return 0;
	else
		return 1;
}

/*
 * Return the duration in seconds between p1 and p2.
 * p1 and p2 should be valid offsets within the data section of the TAP file.
 * An offset in the middle of a TAP v1 longpulse is considered invalid.
 */

static float get_duration(int p1, int p2)
{
	/*long*/ int i;
	unsigned /*long*/ int zsum;
	double tot = 0;
	double p = (double)20000 / cps;
	float apr;

	for (i = p1; i < p2; i++) {

		/* handle normal pulses (non-zeroes) */

		if (tap.tmem[i] != 0)
			tot += ((double)(tap.tmem[i] * 8) / cps);

		/* handle v0 zeroes.. */

		if (tap.tmem[i] == 0 && tap.version == 0)
			tot += p;

		/* handle v1 zeroes.. */

		if (tap.tmem[i] == 0 && tap.version == 1) {
			zsum = (unsigned /*long*/ int) tap.tmem[i + 1] +
				((unsigned /*long*/ int) tap.tmem[i + 2] << 8) +
				((unsigned /*long*/ int) tap.tmem[i + 3] << 16);
			tot += (double) zsum / cps;
			i += 3;
		}
	}

	apr = (float)tot;	/* approximate and return number of seconds. */

	return apr;
}

/*
 * Return the number of unique pulse widths in the TAP.
 * Note: Also fills tap.pst[256] array with distribution stats.
 */

static int get_pulse_stats(int start, int end)
{
	int i, tot, b;

	for (i = 0; i < 256; i++)	/* clear pulse table...  */
		tap.pst[i] = 0;

	/* create pulse table... */

	for (i = start; i < tap.len && i < end; i++) {
		b = tap.tmem[i];
		if (b == 0 && tap.version == 1)
			i += 3;
		else
			tap.pst[b]++;
	}

	/* add up pulse types... */

	tot = 0;

	/* Note the start at 1 (pauses dont count) */

	for (i = 1; i < 256; i++) {
		if (tap.pst[i] != 0)
			tot++;
	}

	return tot;
}

/*
 * Count all file types found in the TAP and their quantities.
 * Also records the number of data files, checksummed data files and gaps in the TAP.
 */

static void get_file_stats(void)
{
	int i;

	for (i = 0; i < 256; i++)	/* init table */
		tap.fst[i] = 0;

	/* count all contained filetype occurences... */

	for (i = 0; blk[i]->lt != LT_NONE; i++)
		tap.fst[blk[i]->lt]++;

	tap.total_data_files = 0;
	tap.total_checksums = 0;
	tap.total_gaps = 0;

	/* for each file format in ft[]...  */

	for (i = 0; ft[i].sp != 666; i++) {
		if (tap.fst[i] != 0) {
			if (ft[i].has_cs == CSYES)
				tap.total_checksums += tap.fst[i];	/* count all available checksums. */
		}

		if (i > PAUSE)
			tap.total_data_files += tap.fst[i];		/* count data files */

		if (i == GAP)
			tap.total_gaps += tap.fst[i];			/* count gaps */
	}
}

/*
 * Print the human readable TAP report to a buffer.
 *
 * Note: this is done so I can send the info to both the screen and the report
 * without repeating the code.
 * Note: Call 'analyze()' before calling this!.
 */

#ifndef TAPCLEAN_EMBEDDED
static void print_results(char *buf)
{
	char szpass[2][5] = {"PASS", "FAIL"};
	char szok[2][5] = {"OK", "FAIL"};
	int min;
	float sec;

	sprintf(buf, "\n\nTAPClean version: "VERSION_STR"\n\nGENERAL INFO AND TEST RESULTS\n");

	sprintf(lin, "\nTAP Name    : %s", tap.path);
	strcat(buf, lin);
	sprintf(lin, "\nTAP Size    : %d bytes (%d kB)", tap.len, tap.len >> 10);
	strcat(buf, lin);
	sprintf(lin, "\nTAP Version : %d", tap.version);
	strcat(buf, lin);
	sprintf(lin, "\nRecognized  : %d%%", tap.detected_percent);
	strcat(buf, lin);
	sprintf(lin, "\nData Files  : %d", tap.total_data_files);
	strcat(buf, lin);
	sprintf(lin, "\nPauses      : %d", database_count_pauses());
	strcat(buf, lin);
	sprintf(lin, "\nGaps        : %d", tap.total_gaps);
	strcat(buf, lin);
	sprintf(lin, "\nMagic CRC32 : %08X", tap.crc);
	strcat(buf, lin);
	min = (int) tap.taptime / 60;
	sec = tap.taptime - min * 60;
	sprintf(lin, "\nTAP Time    : %d:%.2f", min, sec);
	strcat(buf, lin);

	if (tap.bootable) {
		if (tap.bootable == 1)
			sprintf(lin, "\nBootable    : YES (1 part, name: %s)", tap.cbmname);
		else
			sprintf(lin, "\nBootable    : YES (%d parts, 1st name: %s)", tap.bootable, tap.cbmname);
		strcat(buf, lin);
	} else {
		sprintf(lin, "\nBootable    : NO");
		strcat(buf, lin);
	}

	sprintf(lin, "\nLoader ID   : %s", knam[tap.cbmid]);
	strcat(buf, lin);

	/* TEST RESULTS... */

	sprintf(lin, "\n");
	strcat(buf, lin);

	if (tap.tst_hd == 0 && tap.tst_rc == 0 && tap.tst_cs == 0 && tap.tst_rd == 0 && tap.tst_op == 0)
		sprintf(lin, "\nOverall Result    : PASS");
	else
		sprintf(lin, "\nOverall Result    : FAIL");

	strcat(buf, lin);

	sprintf(lin, "\n");
	strcat(buf, lin);
	sprintf(lin, "\nHeader test       : %s [Sig: %s] [Ver: %s] [Siz: %s]", szpass[tap.tst_hd], szok[tap.fsigcheck], szok[tap.fvercheck], szok[tap.fsizcheck]);
	strcat(buf, lin);
	sprintf(lin, "\nRecognition test  : %s [%d of %d bytes accounted for] [%d%%]", szpass[tap.tst_rc], tap.detected, tap.len - 20, tap.detected_percent);
	strcat(buf, lin);
	sprintf(lin, "\nChecksum test     : %s [%d of %d checksummed files OK]", szpass[tap.tst_cs], tap.total_checksums_good, tap.total_checksums);
	strcat(buf, lin);
	sprintf(lin, "\nRead test         : %s [%d Errors]", szpass[tap.tst_rd], tap.total_read_errors);
	strcat(buf, lin);
	sprintf(lin, "\nOptimization test : %s [%d of %d files OK]", szpass[tap.tst_op], tap.optimized_files, tap.total_data_files);
	strcat(buf, lin);
	sprintf(lin, "\n");
	strcat(buf, lin);
}

/*
 * Print pulse stats to buffer 'buf'.
 * Note: Call 'analyze()' before calling this!.
 */

static void print_pulse_stats(char *buf)
{
	int i;

	strcpy(buf, "\nPULSE FREQUENCY TABLE\n");

	for (i = 1; i < 256; i++) {
		if (tap.pst[i] != 0) {
			sprintf(lin, "\n0x%02X (%d)", i, tap.pst[i]);
			strcat(buf, lin);
		}
	}
}

/*
 * Prints out all file info including extra
 * text infos generated by xxxxx_describe functions.
 *
 * Note: defined here as it depends on describe_file(), fstats,
 * get_pulse_stats(), print_pulse_stats().
 */

static void database_print(char *buf, size_t bufsize)
{
	int i;

	sprintf(buf, "\nFILE DATABASE\n");

	for (i = 0; blk[i]->lt != LT_NONE; i++) {
		sprintf(lin, "\n---------------------------------");
		strncat(buf, lin, bufsize - strlen(buf) - 1);
		sprintf(lin, "\nSeq. no.: %d", i + 1);
		strncat(buf, lin, bufsize - strlen(buf) - 1);
		sprintf(lin, "\nFile Type: %s", ft[blk[i]->lt].name);
		strncat(buf, lin, bufsize - strlen(buf) - 1);
		sprintf(lin, "\nLocation: $%04X -> $%04X -> $%04X -> $%04X", blk[i]->p1, blk[i]->p2, blk[i]->p3, blk[i]->p4);
		strncat(buf, lin, bufsize - strlen(buf) - 1);

		if (blk[i]->lt > PAUSE) {		/* info for data files only... */
			sprintf(lin, "\nLA: $%04X  EA: $%04X  SZ: %d", blk[i]->cs, blk[i]->ce, blk[i]->cx);
			strncat(buf, lin, bufsize - strlen(buf) - 1);

			if (blk[i]->fn != NULL) {	/* filename, if applicable */
				sprintf(lin, "\nFile Name: %s", blk[i]->fn);
				strncat(buf, lin, bufsize - strlen(buf) - 1);
			}

			sprintf(lin, "\nPilot/Trailer Size: %d/%d", blk[i]->pilot_len, blk[i]->trail_len);
			strncat(buf, lin, bufsize - strlen(buf) - 1);

			if (ft[blk[i]->lt].has_cs == TRUE) {	/* checkbytes, if applicable */
				sprintf(lin, "\nCheckbyte Actual/Expected: $%02X/$%02X, %s",
					blk[i]->cs_act, blk[i]->cs_exp, blk[i]->cs_act == blk[i]->cs_exp ? "PASS" : "FAIL");
				strncat(buf, lin, bufsize - strlen(buf) - 1);
			}

			sprintf(lin, "\nRead Errors: %d", blk[i]->rd_err);
			strncat(buf, lin, bufsize - strlen(buf) - 1);
			sprintf(lin, "\nUnoptimized Pulses: %d", database_count_unopt_pulses(i));
			strncat(buf, lin, bufsize - strlen(buf) - 1);
			sprintf(lin, "\nCRC32: %08X", blk[i]->crc);
			strncat(buf, lin, bufsize - strlen(buf) - 1);
		}

		strcpy(info, "");	/* clear 'info' ready to receive additional text */
		describe_file(i);
		strncat(buf, info, bufsize - strlen(buf) - 1);	/* add additional text */
		strncat(buf, "\n", bufsize - strlen(buf) - 1);

		if (fstats == TRUE && blk[i]->lt != PAUSE) {		/* info for data files only... */
			get_pulse_stats(blk[i]->p1, blk[i]->p4 + 1);
			print_pulse_stats(info);
			strncat(buf, info, bufsize - strlen(buf) - 1);	/* add additional text */
			strncat(buf, "\n", bufsize - strlen(buf) - 1);
		}

	}
}

/*
 * Print file stats to buffer 'buf'.
 * Note: Call 'analyze()' before calling this!.
 */

static void print_file_stats(char *buf)
{
	int i;

	sprintf(buf, "\nFILE FREQUENCY TABLE\n");

	for (i = 0; ft[i].sp != 666; i++) {	/* for each file format in ft[]...  */
		if (tap.fst[i] != 0) {		/* list all found files and their frequency...  */
			sprintf(lin, "\n%s (%d)", ft[i].name, tap.fst[i]);
			strcat(buf, lin);
		}
	}
}


/**
 *	TAPClean entry point
 */

int main(int argc, char *argv[])
{
	time_t t1, t2;

	/* Delete report and info files */
	delete_work_files();

	/* Get exe path from argv[0] */
	if (!get_exedir(argv[0]))
		return -1;

	/* Assume the dir for reports is the same as where the exe is */
	strcpy(reportdir, exedir);

	/* Allocate database for files (not always needed, but still here) */
	if (!database_create_blk_db())
		return -1;

	/* Pre-calculate CRC table */
	crc32_build_crc_table();

	printf("\n----------------------------------------------------------------------\n");
	printf("TAPClean "VERSION_STR" - "COPYRIGHT_STR" [Built "__DATE__" by "BUILDER_STR"]\n");
	printf("Based on Final TAP 2.76 Console - (C) 2001-2006 Subchrist Software\n");
	printf("----------------------------------------------------------------------\n");

	/* Note: options should be processed before actions! */

	if (argc == 1) {
		display_usage();
		printf("\n");
		cleanup_main();
		return 0;
	}

	process_options(argc, argv);
	handle_cps();

	if (preserveloadervars == TRUE)
		switch (persistence_load_loader_parameters ()) {
			case PERS_OK:
				printf ("\n\nLoader variables restored");
				break;
			case PERS_LOCK_TIMEOUT:
				printf ("\n\nTemporary persistence file found. This might be a dirty leftover: delete and retry");
				break;
			case PERS_UNSUPPORTED_VERSION:
				printf ("\n\nCannot use current version of the persistent store for preserved loader variables");
				break;
		}

	/* PROCESS ACTIONS... */

	/**
	 *	Just test a tap if no option is present, just a filename.
	 *
	 * 	This allows for drag and drop in (Microsoft) explorer.
	 * 	First make sure the argument is not the -b option without
	 * 	arguments, or the -info option.
	 */

	if (argc == 2 && strcmp(argv[1], "-b") && strcmp(argv[1], "-info")) {
		if (load_tap(argv[1])) {
			printf("\n\nLoaded: %s", tap.name);
			printf("\nTesting...\n");
			time(&t1);
			if (analyze()) {
				report();
				printf("\n\nSaved: %s", tcreportname);
				time(&t2);
				time_to_string(t2 - t1, lin);
				printf("\nOperation completed in %s.", lin);
			}
		}
	} else {
		int i;

		char *opname;		/*!< a pointer to one of the following opnames */
		char opnames[][32] = {
			"No operation",
			"Testing",
			"Optimizing",
			"Converting to v0",
			"Converting to v1",
			"Fixing header size",
			"Optimizing pauses",
			"Converting to au file",
			"Converting to wav file",
			"Batch scanning",
			"Creating info file"
		};

		for (i = 0; i < argc; i++) {
			int opnum = OP_NONE;

			if (strcmp(argv[i], "-t") == 0)
				opnum = OP_TEST;	/* test */

			if (strcmp(argv[i], "-o") == 0)
				opnum = OP_OPTIMIZE;	/* optimize */
			if (strcmp(argv[i], "-ct0") == 0)
				opnum = OP_CONVERT_V0;	/* convert to v0 */
			if (strcmp(argv[i], "-ct1") == 0)
				opnum = OP_CONVERT_V1;	/* convert to v1 */

			if (strcmp(argv[i], "-rs") == 0)
				opnum = OP_FIX_HEADER_SIZE;	/* fix header size */
			if (strcmp(argv[i], "-po") == 0)
				opnum = OP_PAUSE_OPTIMIZE;	/* pause optimize */

			if (strcmp(argv[i], "-au") == 0)
				opnum = OP_CONVERT_AU;	/* convert to au */
			if (strcmp(argv[i], "-wav") == 0)
				opnum = OP_CONVERT_WAV;	/* convert to wav */

			if (strcmp(argv[i], "-b") == 0)
				opnum = OP_BATCH_SCAN;	/* batch scan */

			if (strcmp(argv[i], "-info") == 0)
				opnum = OP_CREATE_INFO;	/* create info file */

			opname = opnames[opnum];

			/* This handles testing + any op that takes a tap, affects it and saves it... */

			if (opnum >= OP_TEST && opnum <= OP_PAUSE_OPTIMIZE) {
				if (argv[i + 1] != NULL) {
					if (load_tap(argv[i + 1])) {
						time(&t1);
						printf("\n\nLoaded: %s", tap.name);
						printf("\n%s...\n", opname);

						if (analyze()) {
							if (opnum == OP_OPTIMIZE && tap.total_read_errors && reckless == FALSE) {
								printf("\n\n%s contains read errors so it won't be cleaned.\n", tap.name);
							} else {
								switch(opnum) {
									case OP_OPTIMIZE:
										clean();
										break;
									case OP_CONVERT_V0:
										convert_to_v0();
										break;
									case OP_CONVERT_V1:
										convert_to_v1();
										break;
									case OP_FIX_HEADER_SIZE:
										fix_header_size();
										analyze();
										break;
									case OP_PAUSE_OPTIMIZE:
										convert_to_v0();
										clip_ends();
										unify_pauses();
										convert_to_v1();
										add_trailpause();
										break;
								}

								if (opnum >= OP_OPTIMIZE) {
									char *tapnamepos, *cleanedtapnamefullpath;

									if (opnum == OP_CONVERT_V0 || opnum == OP_CONVERT_V1)
										strcpy(cleanedtapname, CONVERTED_PREFIX);
									else
										strcpy(cleanedtapname, CLEANED_PREFIX);
									strcat(cleanedtapname, tap.name);
									change_file_extention(cleanedtapname, "tap", MAXPATH);
									cleanedtapnamefullpath = cleanedtapname;

									/* Change tap.path because the report refers to the cleaned tap */
									tapnamepos = strstr(tap.path, tap.name);
									if (tapnamepos) {
										strncpy(tapnamepos, cleanedtapname, MAXPATH);
										cleanedtapnamefullpath = tap.path;
									}
									save_tap(cleanedtapnamefullpath);
									printf("\n\nSaved: %s", cleanedtapnamefullpath);
								}
							}

							report();
							printf("\nSaved: %s", tcreportname);

							time(&t2);
							time_to_string(t2 - t1, lin);
							printf("\nOperation completed in %s.", lin);
						}
					}
				} else {
					printf("\n\nMissing file name.");
				}
			}

			if (opnum == OP_CONVERT_AU) {	/* flag = convert to au */
				if (argv[i + 1] != NULL) {
					if (load_tap(argv[i + 1])) {
						if (analyze()) {
							printf("\n\nLoaded: %s", tap.name);
							printf("\n%s...\n", opname);
							tap2audio_au_write(tap.tmem, tap.len, tap2audio_auoutname, sine);
							printf("\nSaved: %s", tap2audio_auoutname);
							msgout("\n");
						}
					}
				} else {
					printf("\n\nMissing file name.");
				}
			}

			if (opnum == OP_CONVERT_WAV) {		/* flag = convert to wav */
				if (argv[i + 1] != NULL) {
					if (load_tap(argv[i + 1])) {
						if (analyze()) {
							printf("\n\nLoaded: %s", tap.name);
							printf("\n%s...\n", opname);
							tap2audio_wav_write(tap.tmem, tap.len, tap2audio_wavoutname, sine);
							printf("\nSaved: %s", tap2audio_wavoutname);
							msgout("\n");
						}
					}
				} else {
					printf("\n\nMissing file name.");
				}
			}

			if (opnum == OP_BATCH_SCAN) {		/* flag = batch scan... */
				char fstats_old = fstats;

				batchmode = TRUE;
				quiet = TRUE;

				if (argv[i + 1] != NULL) {
					printf("\n\nBatch Scanning: %s\n", argv[i + 1]);
					fstats = FALSE;
					batchscan(argv[i + 1], incsubdirs, 1);
				} else {
					printf("\n\nMissing directory name, using current.");
					printf("\n\nBatch Scanning: %s\n", exedir);
					batchscan(exedir, incsubdirs, 1);
				}

				fstats = fstats_old;
				batchmode = FALSE;
				quiet = FALSE;
			}

			/* flag = generate exe info file */

			if (opnum == OP_CREATE_INFO) {
				FILE *fp;

				fp = fopen(tcinfoname, "w+t");
				if (fp != NULL) {
					printf("\n%s...\n", opname);
					fprintf(fp, "TAPClean %s", VERSION_STR);
					fclose(fp);
				}
			}
		}
	}

	if (preserveloadervars == TRUE) {
		switch (persistence_save_loader_parameters ()) {
			case PERS_OK:
				printf ("\n\nLoader variables stored");
				break;
			case PERS_LOCK_TIMEOUT:
				printf ("\n\nTemporary persistence file found. This might be a dirty leftover: delete and retry");
				break;
			case PERS_OPEN_ERROR:
				printf ("\n\nError opening temporary persistent store for preserved loader variables");
				break;
			case PERS_IO_ERROR:
				printf ("\n\nError copying temporary data to the persistent store for preserved loader variables");
				break;
		}
	}

	printf("\n\n");
	cleanup_main();

	return 0;
}
#endif /* !TAPCLEAN_EMBEDDED */

/*
 * Read 1 pulse from the tap file offset at 'pos', decide whether it is a Bit0 or Bit1
 * according to the values in the parameters. (+/- global tolerance value 'tol')
 * lp = ideal long pulse width.
 * sp = ideal short pulse width.
 * tp = threshold pulse width (can be NA if unknown)
 *
 * Return (bit value) 0 or 1 on success, else -1 on failure.
 *
 * Note: Most formats can use this (and readttbyte()) for reading data, but some
 * (ie. cbmtape, pavloda, visiload,supertape etc) have custom readers held in their
 * own source files.
 */

int readttbit(int pos, int lp, int sp, int tp)
{
	int valid, v, b;

#ifdef TAPCLEAN_EMBEDDED
	/* Scans are CPU-bound for seconds at a time; breathe periodically so
	   the idle task (task watchdog) and lower-priority tasks can run */
	{
		static unsigned int yield_ctr;

		if ((++yield_ctr & 0x3FFFF) == 0)
			tapclean_scan_yield();
	}
#endif

	if (skewadapt_enabled && tp != NA)
		return skewadapt_readttbit(pos, lp, sp, tp);

	if (pos < 20 || pos > tap.len - 1)	/* return error if out of bounds.. */
		return -1;

	if (is_pause_param(pos))		/* return error if offset is on a pause.. */
		return -1;

	valid = 0;
	b = tap.tmem[pos];

	if (tp != NA) {				/* exact threshold pulse IS available... */
		if (b < tp && b > sp - tol) {	/* its a SHORT (Bit0) pulse... */
			v = 0;
			valid += 1;
		}

		if (b > tp && b < lp + tol) {	/* its a LONG (Bit1) pulse... */
			v = 1;
			valid += 2;
		}

		if (b == tp)			/* its ON the threshold!... */
			valid += 4;
	} else {				/* threshold unknown? ...use midpoint method... */
		if (b > (sp - tol) && b < (sp + tol)) {	/* its a SHORT (Bit0) pulse...*/
			valid += 1;
			v = 0;
		}

		if (b > (lp - tol) && b < (lp + tol)) {	/* its a LONG (Bit1) pulse... */
			valid += 2;
			v = 1;
		}

		if (valid == 3) {		/* pulse qualified as 0 AND 1!, use closest match... */
			if ((abs(lp - b)) > (abs(sp - b)))
				v = 0;
			else
				v = 1;
		}
	}

	if (valid == 0) {			/* Error, pulse didnt qualify as either Bit0 or Bit1... */
		add_read_error(pos);
		v = -1;
	}

	if (valid == 4) {			/* Error, pulse is ON the threshold... */
		add_read_error(pos);
		v = -1;
	}

	return v;
}

/*
 * Generic "READ_BYTE" function, (can be used by most turbotape formats)
 * Reads and decodes 8 pulses from 'pos' in the TAP file.
 * parameters...
 * lp = ideal long pulse width.
 * sp = ideal short pulse width.
 * tp = threshold pulse width (can be NA if unknown)
 * return 0 or 1 on success else -1.
 * endi = endianness (use constants LSbF or MSbF).
 * Returns byte value on success, or -1 on failure.
 *
 * Note: Most formats can use this (and readttbit) for reading data, but some
 * (ie. cbmtape, pavloda, visiload,supertape etc) have custom readers held in their
 * own source files.
 */

int readttbyte(int pos, int lp, int sp, int tp, int endi)
{
	int i, v, b;
	unsigned char byte[10];

	/* check next 8 pulses are not inside a pause and *are* inside the TAP... */

	for(i = 0; i < 8; i++) {
		b = readttbit(pos + i, lp, sp, tp);
		if (b == -1)
			return -1;
		else
			byte[i] = b;
	}

	/* if we get this far, we have 8 valid bits... decode the byte... */

	if (endi == MSbF) {
		v = 0;
		for (i = 0; i < 8; i++) {
			if (byte[i] == 1)
				v += (128 >> i);
		}
	} else {
		v = 0;
		for (i = 0; i < 8; i++) {
			if (byte[i] == 1)
			v += (1 << i);
		}
	}

	return v;
}

/*---------------------------------------------------------------------------
 * Search for pilot/sync sequence at offset 'pos' in tap file...
 * 'fmt' should be the numeric ID (or constant) of a format described in ft[].
 *
 * Returns 0 if no pilot found.
 * Returns end position if pilot found (and a legal quantity of them).
 * Returns end position (negatived) if pilot found but too few/many.
 *
 * Note: End position = file offset of 1st NON pilot value, ie. a sync byte.
 */

int find_pilot(int pos, int fmt)
{
#ifdef TAPCLEAN_EMBEDDED
	/* See readttbit(): scanners with custom bit readers still call
	   find_pilot per tape offset, so yield here too */
	{
		static unsigned int yield_ctr;

		if ((++yield_ctr & 0x3FFFF) == 0)
			tapclean_scan_yield();
	}
#endif
	int z, sp, lp, tp, en, pv, sv, pmin, pmax;

	if (pos < 20)
		return 0;

	sp = ft[fmt].sp;
	lp = ft[fmt].lp;
	tp = ft[fmt].tp;
	en = ft[fmt].en;
	pv = ft[fmt].pv;
	sv = ft[fmt].sv;
	pmin = ft[fmt].pmin;
	pmax = ft[fmt].pmax;

	if (pmax == NA)
		pmax = 200000;		/* set some crazy limit if pmax is NA (NA=0) */

	if ((pv == 0 || pv == 1) && (sv == 0 || sv == 1)) {	/* are the pilot/sync BIT values?... */
		if (readttbit(pos, lp, sp, tp) == pv) {		/* got pilot bit? */
			z = 0;
			while(readttbit(pos, lp, sp, tp) == pv && pos < tap.len) {	/* skip all pilot bits.. */
				z++;
				pos++;
			}

			if (z == 0)
				return 0;

			if (z < pmin || z > pmax)	/* too few/many pilots?, return position as negative. */
				return -pos;

			if (z >= pmin && z <= pmax)	/* enough pilots?, return position. */
				return pos;
		}
	} else {	/* Pilot/sync are BYTE values... */
		if (readttbyte(pos, lp, sp, tp, en) == pv) {	/* got pilot byte? */
			z = 0;
			while(readttbyte(pos, lp, sp, tp, en) == pv && pos < tap.len) {	/* skip all pilot bytes.. */
				z++;
				pos += 8;
			}

			if (z == 0)
				return 0;

			if (z < pmin || z > pmax)	/* too few/many pilots?, return position as negative. */
				return -pos;

			if (z >= pmin && z <= pmax)	/* enough pilots?, return position. */
				return pos;
		}
	}

	return 0;
}

int find_pilot_bytes_ex(int pos, int fmt, readbyteproc_t readbyte_usr, int bitsinabyte)
{
	int z, sp, lp, tp, en, pv, pmin, pmax;

	if (pos < 20)
		return 0;

	sp = ft[fmt].sp;
	lp = ft[fmt].lp;
	tp = ft[fmt].tp;
	en = ft[fmt].en;
	pv = ft[fmt].pv;
	pmin = ft[fmt].pmin;
	pmax = ft[fmt].pmax;

	if (pmax == NA)
		pmax = 200000;		/* set some crazy limit if pmax is NA (NA=0) */

	if ((readbyte_usr)(pos, lp, sp, tp, en) == pv) {	/* got pilot byte? */
		z = 0;
		while((readbyte_usr)(pos, lp, sp, tp, en) == pv && pos < tap.len) {	/* skip all pilot bytes.. */
			z++;
			pos += bitsinabyte;
		}

		if (z == 0)
			return 0;

		if (z < pmin || z > pmax)	/* too few/many pilots?, return position as negative. */
			return -pos;

		if (z >= pmin && z <= pmax)	/* enough pilots?, return position. */
			return pos;
	}

	return 0;
}

void calculate_averages_in_pilot(int start, int end, int threshold, int *sp, int *lp)
{
	int pos, b;
	int sp_sum, sp_tot, lp_sum, lp_tot;


	sp_sum = sp_tot = lp_sum = lp_tot = 0;
	*sp = *lp = 0;

	for (pos = start; pos <= end; pos++) {
		b = tap.tmem[pos];
		if (b > threshold) {
			lp_sum += b;
			lp_tot++;
		} else {
			sp_sum += b;
			sp_tot++;
		}
	}
	if (sp_tot)
		*sp = sp_sum / sp_tot;
	if (lp_tot)
		*lp = lp_sum / lp_tot;
}

/*
 * Load the named tap file into buffer (tap.tmem[])
 *
 * @param name	Name of the tap file to load
 *
 * @return 1 on success
 * @return 0 on error.
 */

int load_tap(char *name)
{
	FILE *fp;
	size_t flen;
	unsigned char *input_buffer, *output_buffer;

	fp = fopen(name, "rb");
	if (fp == NULL) {
		msgout("\nError: Couldn't open file in load_tap().");
		return 0;
	}

	/* First erase all stored data for the loaded 'tap', free buffers,
	   and reset database entries */
	unload_tap();

	/* Read file size */
	fseek(fp, 0, SEEK_END);
	flen = ftell(fp);
	rewind(fp);

	/* Allocate enough space to load the file into */
	input_buffer = (unsigned char*)malloc(flen);
	if (input_buffer == NULL) {
		msgout("\nError: malloc failed in load_tap().");
		fclose(fp);
		return 0;
	}

	/* Load data into buffer */
	fread(input_buffer, flen, 1, fp);
	fclose(fp);

	/* Check for DC2N format */
	if (strncmp(DC2N_ID_STRING, (char *)input_buffer, strlen(DC2N_ID_STRING)) == 0) {
		msgout("\nDC2N format detected. Converting to legacy TAP v1 (assuming 16-bit and 2MHz).");

		output_buffer = (unsigned char*)malloc(flen);
		if (output_buffer == NULL) {
			msgout("\nError: malloc failed in load_tap().");
			free (input_buffer);
			return 0;
		}

		tap.len = dc2nconv_to_tap(input_buffer, output_buffer, (int)flen);
		tap.tmem = output_buffer;
		free (input_buffer);
	} else {
		tap.tmem = input_buffer;

		/* Set the 'tap' structure file length subfield */
		tap.len = flen;
	}


	/* the following were uncommented in GUI app..
	 * these now appear in main()...
	 * tol=DEFTOL;        reset tolerance..
	 * debugmode=FALSE;      reset "debugging" mode.
	 */


	/* Makes sure the tap is fully scanned afterwards. */
	tap.changed = TRUE;

	/* Reset this so cbm parts decode for a new tap
	   (but NOT during same tap) */
	cbm_decoded = FALSE;

	/* Set the 'tap' structure file path and name subfields */
	strcpy(tap.path, name);
	getfilename(tap.name, name);

	/* Enable skew adapting in case the command line option was given.
	 * Re-enable it if more than one input file was selected for analysis.
	 */
	if (skewadapt)
		skewadapt_enabled = TRUE;

	return 1;		/* 1 = loaded ok */
}

/**
 * Perform a full analysis of the tap file
 *
 * Gather all available info from the tap. Most data is stored in the 'tap' struct.
 *
 * Text output for pulse and file stats are written to str_pulses[] & str_files[] (global char arrays)
 * Note: this text output is created for the benefit of batchmode. (which doesnt call report()).
 *
 * Return 0 if file is not a valid TAP, else 1.
 */

int analyze(void)
{
	double per;

	if (tap.tmem == NULL)
		return 0;		/* no tap file loaded */

	tap.fsigcheck = check_signature();
	tap.fvercheck = check_version();
	tap.fsizcheck = check_size();

	if (tap.fsigcheck == 1 && tap.fvercheck == 1 && tap.fsizcheck == 1) {
		msgout("\nError: File is not a valid C64 TAP.");	/* all header checks failed */
		return 0;
	}

	tap.taptime = get_duration(20, tap.len);

	/* While cleaning, analyze() is called very often: the same
	   information is appended to the 'info' buffer continuously.
	   This may lead to buffer overflow (Super Man, Visiload T1) so
	   that we empty the buffer here for it's not used anyway */
	strcpy(info, "");	/* clear 'info' ready to receive new text */

	/* now call search_tap() to fill the file database (blk) */
	/* + call describe_blocks() to extract data and get checksum info. */

	note_errors = FALSE;

	search_tap();

	note_errors = TRUE;

	/*
	 * Calls to find_decode_block() can set tap.cbmname incorrectly, e.g.
	 * in Visiload, according to which CBM Header is looked for first, so 
	 * we force the first CBM Header filename discovery by resetting 
	 * tap.cbmname.
	 */
	strcpy(tap.cbmname, "");

	describe_blocks();

	note_errors = FALSE;

	/* Gather statistics... */

	if (fstats == FALSE)
		tap.purity = get_pulse_stats(20, tap.len - 1);

	get_file_stats();

	tap.optimized_files = database_count_opt_blks();
	tap.total_checksums_good = database_count_good_checkbytes();
	tap.detected = database_count_recognized_pulses();
	tap.bootable = database_count_bootparts();

	/* compute % recognised... */

	per = ((double)tap.detected / ((double)tap.len - 20)) * 100;
	tap.detected_percent = (int) floor(per);

	/* Compute & store quality checks... */

	if (tap.fsigcheck == 0 && tap.fvercheck == 0 && tap.fsizcheck == 0)
		tap.tst_hd = 0;
	else
		tap.tst_hd = 1;

	if (tap.detected == (tap.len - 20))
		tap.tst_rc = 0;
	else
		tap.tst_rc = 1;

	if (tap.total_checksums_good == tap.total_checksums)
		tap.tst_cs = 0;
	else
		tap.tst_cs = 1;

	if (tap.total_read_errors == 0)
		tap.tst_rd = 0;
	else
		tap.tst_rd = 1;

	if (tap.total_data_files - tap.optimized_files == 0)
		tap.tst_op = 0;
	else
		tap.tst_op = 1;

	tap.crc = database_compute_overall_crc();

	return 1;
}

#ifndef TAPCLEAN_EMBEDDED
/*
 * Save a report for this TAP file.
 * Note: Call 'analyze()' before calling this!.
 */

void report(void)
{
#define RBUFSIZE	1000000
	int i;
	FILE *fp;
	char *rbuf;

	rbuf = (char*)malloc(RBUFSIZE);
	if (rbuf == NULL) {
		msgout("\nError: malloc failed in report().");
		exit(1);
	}

	chdir(reportdir);

	fp = fopen(temptcreportname, "r");	/* delete any existing temp file... */
	if (fp != NULL) {
		fclose(fp);
		unlink (temptcreportname);
	}

	fp = fopen(tcreportname, "r");		/* delete any existing report file... */
	if (fp != NULL) {
		fclose(fp);
		unlink (tcreportname);
	}

	fp = fopen(temptcreportname, "w+t");	/* create new report file... */

	if (fp != NULL) {

		/* include results and general info... */

		print_results(rbuf);
		fprintf(fp, "%s", rbuf);
		fprintf(fp, "\n\n\n\n\n");

		/* include file stats... */

		print_file_stats(rbuf);
		fprintf(fp, "%s", rbuf);
		fprintf(fp, "\n\n\n\n\n");

		/* include database in the file (partially interpreted)... */

		database_print(rbuf, RBUFSIZE);

		if (fstats == FALSE) {
			fprintf(fp, "%s", rbuf);
			fprintf(fp, "\n\n\n\n\n");

			/* include pulse stats in the file... */

			print_pulse_stats(rbuf);
		}
		fprintf(fp, "%s", rbuf);
		fprintf(fp, "\n\n\n\n\n");

		/* include 'read errors' report in the file... */

		if (tap.total_read_errors != 0) {
			fprintf(fp, "\n * Read error locations (Max %d)", NUM_READ_ERRORS);
			fprintf(fp, "\n");
			for (i = 0; read_errors[i] != 0; i++)
				fprintf(fp, "\n0x%04X", read_errors[i]);
			fprintf(fp, "\n\n\n\n\n");
		}
		fclose(fp);

		//sprintf(lin, OSAPI_RENAME_FILE" %s %s", temptcreportname, tcreportname);
		//system(lin);
		rename (temptcreportname, tcreportname);
	} else {
		msgout("\nError: failed to create report file.");
	}

	/* show results and general info onscreen... */

	if (!batchmode) {
		print_results(rbuf);
		fprintf(stdout, "%s", rbuf);
	}

	free(rbuf);

	if (doprg == TRUE) {
		database_make_prg_db();
		database_save_prg_db();
	}
}

/*
 * Calls upon functions found in clean.c to optimize and
 * correct the TAP.
 */

void clean(void)
{
	int x;

	if (tap.tmem == NULL) {
		msgout("\nError: No TAP file loaded!.");
		return;
	}

	if (debug) {
		msgout("\nError: Optimization is disabled whilst in debugging mode.");
		return;
	}

	quiet = 1;		/* no talking between search routines and worklog */

	convert_to_v0();	/* unpack pauses to V0 format if not already */
	clip_ends();		/* clip leading and trailing pauses */
	unify_pauses();		/* connect/rebuild any consecutive pauses */
	clean_files();		/* force perfect pulsewidths on known blocks */
	convert_to_v1();	/* repack pauses (v1 format) */

	fill_cbm_tone();	/* presently fills any gap of around 80 pulses   */
				/* (following a CBM block) with ft[CBM_HEAD].sp's. */

	/* this loop repairs pilot bytes and small gaps surrounding pauses, if
	 * any pauses are inserted by 'insert_pauses()' then new gaps and pilots
	 * may be identified in which case we repeat the loop until they are all
	 * dealt with.
	 */
	do {
		fix_pilots();		/* replace broken pilots with new ones. */
		fix_prepausegaps();	/* fix all pre pause "spike runs" of 1 2 or 3. */
		fix_postpausegaps();	/* fix all post pause "spike runs" of 1 2 or 3. */
		x = insert_pauses();	/* insert pauses between blocks that need one. */
	} while(x);

	standardize_pauses();		/* standardize CBM HEAD -> CBM PROG pauses. */
	fix_boot_pilot();		/* add new $6A00 pulse pilot on a CBM boot header. */
	cut_postdata_gaps();		/* cuts post-data gaps <20 pulses. */
	cut_leading_gap();              /* cuts gap <20 pulses found at beginning of tape. */

	if (noaddpause == FALSE)
		add_trailpause();	/* add a 5 second trailing pause   */

	fix_bleep_pilots();		/* correct any corrupted bleepload pilots */
	fix_pavloda_check_bytes();	/* correct any corrupted pavloda check bytes */

	msgout("\n");
	msgout("\nCleaning finished.");

	quiet = 0;			/* allow talking again. */
}
#endif /* !TAPCLEAN_EMBEDDED */

/*
 * Check whether the offset 'x' is accounted for in the database (by a data file
 * or a pause, not a gap), return 1 if it is, else 0.
 */

int is_accounted(int x)
{
	int i;

	for (i = 0; blk[i]->lt != LT_NONE; i++) {
		if (blk[i]->lt != GAP) {
			if ((x >= blk[i]->p1) && (x <= blk[i]->p4))
				return 1;
		}
	}

	return 0;
}

/*
 * Checks whether location 'p' is inside a pause. (harder than it sounds!)
 * Return 1 if it is, 0 if not.
 * Returns -1 if index is out-of-bounds
 */

int is_pause_param(int p)
{
	int i, z, pos;

	if (p < 20 || p > tap.len - 1)	/* p is out of bounds  */
		return -1;

	if (tap.tmem[p] == 0)		/* p is pointing at a zero! */
		return 1;

	if (tap.version == 0)		/* previous 'if' would have dealt with v0. the rest is v1 only */
		return 0;

	if (p < 24) {			/* test very beginning of TAP file, ensures no rewind into header! */
		if (tap.tmem[20] == 0)
			return 1;
		else
			return 0;
	}

	pos = p - 4;			/* pos will be at least 20 */

	do {				/* find first 4 pulses containing no zeroes (behind p)... */
		z = 0;
		for (i = 0; i < 4; i++) {
			if(tap.tmem[pos + i] == 0)
				z++;
		}
		if (z != 0)
			pos--;
	} while(z != 0 && pos > 19);


	if (z == 0) {			/* if TRUE, we found the first 4 containing no zeroes (behind p) */
		pos += 4;		/* pos now points to first v1 pause (a zero)  */

		/* ie.          xxxxxxxxxxx 0xx0 00xx 0x0x x 0x0x 00xx */
		/*			    ^=pos          ^ = p */

		for (i = pos; i < tap.len - 4 ; i++) {
			if (tap.tmem[i] == 0)	/* skip over v1 pauses */
				i += 3;
			else {
				if (i == p)
					return 0;	/* p is not in a pause */

				if (i > p)
					return 1;	/* p is in a pause */
			}
		}
	} else { /* luigi: just start at the beginning then */
		for (i = 20; i < tap.len - 4 ; i++) {
			if (tap.tmem[i] == 0)	/* skip over v1 pauses */
				i += 3;
			else {
				if (i == p)
					return 0;	/* p is not in a pause */

				if (i > p)
					return 1;	/* p is in a pause */
			}
		}
	}

	return 0; /* p is the pos of one of the last 4 pulses in the TAP file */
}

/*
 * search blk[] array for instance number 'num' of block type 'lt' and calls
 * 'xxx_describe_block' for that file (which decodes it).
 * this is for use by scanners which need to get access to data held in other files
 * ahead of describe() time.
 * returns the block number in blk[] of the matching (and now decoded) file.
 * on failure returns -1;
 *
 * NOTE : currently only implemented for certain file types.
 */

int find_decode_block(int lt, int num)
{
	int i, j;

	for (i = 0,j = 0; i < BLKMAX; i++) {
		if (blk[i]->lt == lt) {
			j++;
			if (j == num) {		/* right filetype and right instance number? */
				if (lt == CBM_DATA || lt == CBM_HEAD) {
					cbm_describe(i);
					return i;
				}

				if (lt == CYBER_F1) {
					cyberload_f1_describe(i);
					return i;
				}

				if (lt == CYBER_F2) {
					cyberload_f2_describe(i);
					return i;
				}
			}
		}
	}

	return -1;
}

/*
 * Add an entry to the 'read_errors[NUM_READ_ERRORS]' array...
 */

int add_read_error(int addr)
{
	int i;

	if (!note_errors)
		return -1;

	for (i = 0; i < NUM_READ_ERRORS; i++) {				/* reject duplicates.. */
		if (read_errors[i] == addr)
			return -1;
	}

	for (i = 0; read_errors[i] != 0 && i < NUM_READ_ERRORS; i++);	/* find 1st free slot.. */

	if (i < NUM_READ_ERRORS) {
		read_errors[i] = addr;
		return 0;
	}

	return -1;		/* -1 = error table is full */
}

/*
 * Displays a message.
 * I made this to quickly convert the method of text output from the windows
 * sources. (ie. popup message windows etc).
 */

void msgout(char *str)
{
	printf("%s", str);
}

/*
 * Search integer array 'buf' for occurrence of sequence 'seq'.
 * On success return offset of matching sequence.
 * On failure return -1.
 * Note : value XX (-1) may be used in 'seq' as a wildcard.
 */

int find_seq(int *buf, int bufsz, int *seq, int seqsz)
{
	int i, j, match;

	if (seqsz > bufsz)			/* buf must be larger or equal to seq */
		return -1;

	for (i = 0; i < bufsz - seqsz; i++) {
		if (buf[i] == seq[0]) {		/* match first number. */
			match = 0;
			for (j = 0; j < seqsz && (i + j) < bufsz; j++) {
				if (buf[i + j] == seq[j] || seq[j] == -1)
					match++;
			}
			if (match == seqsz)	/* whole sequence found?  */
				return i;
		}
	}

	return -1;
}

/*
 * Isolate the filename part of a full path+filename and store it in buffer *dest.
 */

void getfilename(char *dest, char *fullpath)
{
	char *pch;

	pch = strrchr(fullpath, '/');
	if (pch == NULL)
		pch = strrchr(fullpath, '\\');

	strcpy (dest, pch ? pch+1 : fullpath);
}

/*
 * Change file extension if any is already in filename
 */

char* change_file_extention(char *filename, char *new_extension, int buffer_length)
{
	int i, len;

	len = strlen(filename);
	for (i = len - 1; i > 0 && filename[i] != '.'; i--)
		;

	if (filename[i] == '.') {
		int j;

	        i++;
	        len = strlen(new_extension);
	        for (j = 0; j < len && i+j < buffer_length - 1; j++)
	                filename[i+j] = new_extension[j];
		filename[i+j] = '\0';
	}

	return filename;
}

/*
 * convert PetASCII string to ASCII text string.
 * user provides destination storage string 'dest'.
 * function returns a pointer to dest so the function may be called inline.
 */

char* pet2text(char *dest, char *src)
{
	int i, lwr;
	char ts[500];
	unsigned char ch;

	lwr = 0;	/* lowercase off. */

	/* process file name... */

	strcpy(dest, "");
	for (i = 0; src[i] != 0; i++) {
		ch = (unsigned char)src[i];

		/* process CHR$ 'SAME AS' codes... */

		if (ch == 255)
			ch = 126;
		if (ch > 223 && ch < 255)	/* produces 160-190 */
			ch -= 64;
		if (ch > 191 && ch < 224)	/* produces 96-127 */
			ch -= 96;

		if (ch == 14)			/* switch to lowercase.. */
			lwr = 1;
		if (ch == 142)			/* switch to uppercase.. */
			lwr = 0;

		if (ch > 31 && ch < 128) {	/* print printable character... */
			if (lwr) {		/* lowercase?, do some conversion... */
				if (ch > 64 && ch < 91)
					ch += 32;
				else if (ch > 96 && ch < 123)
					ch -= 32;
			}

			sprintf(ts, "%c", ch);
			strcat(dest, ts);
		}
	}

	return dest;
}

/*
 * Only keeps alphanumeric chars in a string so that it is safe to use it as filename.
 */

void fname_text(char *src)
{
	char *s;

	for (s = src; *s; s++) {
		if (!isalnum(*s))
			*s = '_';	/* convert the rest to underscore */
	}
}

/*
 * Trims trailing spaces from a string.
 */

void trim_string(char *str)
{
	int i, len;

	len = strlen(str);
	if (len > 0) {
		for (i = len - 1; str[i] == 32 && i > 0; i--)
			str[i] = 0;	/* nullify trailing spaces.  */
	}
}

/*
 * Converts an integer number of seconds to a time string of format HH:MM:SS.
 */

void time_to_string(time_t secs, char *buf)
{
	int h, m, s;

	h = (int) (secs / 3600);
	m = (int) ((secs % 3600) / 60);
	s = (int) (secs % 60);
	sprintf(buf, "%02d:%02d:%02d", h, m, s);
}

#ifndef TAPCLEAN_EMBEDDED
/*
 * Remove all/any existing work files.
 */

void delete_work_files(void)
{
	FILE *fp;

	/* delete existing work files... */
	/* note: the fopen tests avoid getting any console error output. */

	fp = fopen(temptcreportname, "r");
	if (fp != NULL) {
		fclose(fp);
		unlink (temptcreportname);
	}

	fp = fopen(tcreportname, "r");
	if (fp != NULL) {
		fclose(fp);
		unlink (tcreportname);
	}

	fp = fopen(temptcbatchreportname, "r");
	if (fp != NULL) {
		fclose(fp);
		unlink (temptcbatchreportname);
	}

	fp = fopen(tcbatchreportname, "r");
	if (fp != NULL) {
		fclose(fp);
		unlink (tcbatchreportname);
	}

	fp = fopen(tcinfoname, "r");
	if (fp != NULL) {
		fclose(fp);
		unlink (tcinfoname);
	}
}
#endif /* !TAPCLEAN_EMBEDDED */

#ifdef TAPCLEAN_EMBEDDED
/*
 * Embedded-library glue (tapclean_api.c is the public surface). These live
 * here because they need main.c statics (unload_tap, get_duration).
 */

/* Mirror of load_tap() for an in-memory image; takes ownership of 'buf'. */
int tapclean_embedded_load(unsigned char *buf, unsigned int len)
{
	unsigned char *output_buffer;

	unload_tap();

	/* Check for DC2N format */
	if (len >= strlen(DC2N_ID_STRING) &&
	    strncmp(DC2N_ID_STRING, (char *)buf, strlen(DC2N_ID_STRING)) == 0) {
		output_buffer = (unsigned char*)malloc(len);
		if (output_buffer == NULL) {
			msgout("\nError: malloc failed in tapclean_embedded_load().");
			free(buf);
			return 0;
		}

		tap.len = dc2nconv_to_tap(buf, output_buffer, (int)len);
		tap.tmem = output_buffer;
		free(buf);
	} else {
		tap.tmem = buf;
		tap.len = len;
	}

	tap.changed = TRUE;
	cbm_decoded = FALSE;

	strcpy(tap.path, "buffer.tap");
	strcpy(tap.name, "buffer.tap");

	if (skewadapt)
		skewadapt_enabled = TRUE;

	return 1;
}

void tapclean_embedded_unload(void)
{
	unload_tap();
}

/* Play time in seconds between two TAP offsets (see get_duration) */
float tapclean_embedded_duration(int p1, int p2)
{
	return get_duration(p1, p2);
}

/* Inverse of get_duration(): TAP offset reached after 'ms' of play time
   from the start of the data area (offset 20). Used for the Meatloaf tape
   counter. Walks pulses exactly like get_duration(). */
int tapclean_embedded_offset_at_ms(unsigned int ms)
{
	int i;
	unsigned int zsum;
	double tot = 0;
	double target = (double)ms / 1000.0;
	double p = (double)20000 / cps;

	if (tap.tmem == NULL)
		return 0;

	for (i = 20; i < tap.len; i++) {
		if (tot >= target)
			return i;

		if (tap.tmem[i] != 0)
			tot += ((double)(tap.tmem[i] * 8) / cps);

		if (tap.tmem[i] == 0 && tap.version == 0)
			tot += p;

		if (tap.tmem[i] == 0 && tap.version == 1) {
			zsum = (unsigned int) tap.tmem[i + 1] +
				((unsigned int) tap.tmem[i + 2] << 8) +
				((unsigned int) tap.tmem[i + 3] << 16);
			tot += (double) zsum / cps;
			i += 3;
		}
	}

	return tap.len;
}
#endif /* TAPCLEAN_EMBEDDED */

