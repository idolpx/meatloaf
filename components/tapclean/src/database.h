/**
 *	@file 	database.h
 *	@brief	A tap database for recognized files and PRGs.
 *
 *	Details here.
 */

#ifndef __BLKDATABASE_H__
#define __BLKDATABASE_H__

#define BLKMAX	2000	/*!< maximum number of blocks allowed in database */
#define DBERR	-1	/*!< return value from "database_add_blk_def" when database
			     entry failed. */
#define DBFULL	-2	/*!< return value from "database_add_blk_def" when database
			     is full. */
#define HASNOTCHECKSUM	-2

/**
 *	Struct 'blk_t'
 *
 *	This is the basic unit of the tap database, each entity found in
 *	a tap file will have one of these, see array 'blk[]'...
 */

struct blk_t
{
	int lt;			/*!< loader type (see enum of
	    			     constants GAP, PAUSE, CBM_HEAD, etc.) */
	int p1;			/*!< first pulse offset (should be long) */
	int p2;			/*!< first data pulse offset (should be long) */
	int p3;			/*!< last data pulse offset (should be long) */
	int p4;			/*!< last pulse offset (should be long) */
	int xi;			/*!< extra info (should be long) */
	int meta1;		/*!< meta info 1 (should be long) */

	int cs;			/*!< c64 RAM start pos (16 bit value) */
	int ce;			/*!< c64 RAM end pos (16 bit value) */
	int cx;			/*!< c64 RAM len (16 bit value) */

	unsigned char *dd;	/*!< pointer to decoded data block */

	int crc;		/*!< crc32 of the decoded data file (should be
				     ulong) */
	int rd_err;		/*!< number of read errors in block (should be
				     long) */

	int cs_exp;		/*!< expected checksum value (if applicable) */
	int cs_act;		/*!< actual checksum value (if applicable) */

	int pilot_len;		/*!< length of pilot tone (in bytes or pulses)
				     (should be long) */
	int trail_len;		/*!< length of trail tone (in bytes or pulses)
				     (should be long) */

	char *fn;		/*!< pointer to ASCII file name (if applicable) */
	int ok;			/*!< file ok indicator, 1=ok. */
};

/**
 *	Struct 'prg_t'
 *
 *	It contains an extracted data file and infos for it,
 *	used as array 'prg[]'
 */

struct prg_t
{
	int blkidstart;		/*!< database block where PRG contents start */
#ifdef TAPCLEAN_EMBEDDED
	int blkidend;		/*!< database block where PRG contents end
				     (== blkidstart unless blocks were united) */
#endif

	int lt;			/*!< loader type (required for block
				     unification) */

	int cs;			/*!< c64 RAM start pos (16 bit value) */
	int ce;			/*!< c64 RAM end pos (16 bit value) */
	int cx;			/*!< c64 RAM len (16 bit value) */

	char *fn;		/*!< pointer to ASCII file name (if applicable) */
	unsigned char *dd;	/*!< pointer to decoded data block */

	int errors;		/*!< number of read errors in the file (should
				     be long) */
};

#ifdef TAPCLEAN_EMBEDDED
/* Both databases are heap-allocated (PSRAM) by tapclean_init() */
extern struct blk_t **blk;
extern struct prg_t *prg;
#else
/* Database of all found entities. */
extern struct blk_t *blk[BLKMAX];
/* Database of all extracted files (prg's). */
extern struct prg_t prg[BLKMAX];
#endif
/* Flag used by database_add_blk_def() to indicate database capacity reached */
extern int database_is_full;


/**
 *	Prototypes
 */

/* File Database */
int database_create_blk_db(void);
void database_reset_blk_db(void);
int database_add_blk_def_ex(int, int, int, int, int, int, int);
int database_add_blk_def(int, int, int, int, int, int);
void database_sort_blks(void);
void database_scan_gaps(void);
int database_count_bootparts(void);
int database_count_unopt_pulses(int);
int database_count_opt_blks(void);
int database_count_pauses(void);
int database_count_recognized_pulses(void);
int database_count_good_checkbytes(void);
int database_compute_overall_crc(void);
void database_dump_blk_db(void);
void database_destroy_blk_db(void);

/* Prg Database */
void database_make_prg_db(void);
int database_save_prg_db(void);
void database_reset_prg_db(void);

/**
 *	Aliases (used so that all scanners won't go through a mass search/replace, yet)
 */

#define addblockdefex	database_add_blk_def_ex
#define addblockdef	database_add_blk_def

#endif /* __BLKDATABASE_H__ */
