/**
 *	@file 	database.c
 *	@brief	A tap database for recognized files and prg data.
 *
 *	Details here.
 */

#ifdef WIN32
#include <io.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "mydefs.h"
#include "database.h"
#include "main.h"

#ifdef TAPCLEAN_EMBEDDED
struct blk_t **blk;		/*!< heap-allocated by tapclean_init() (the
				     8 KB pointer array is too much BSS) */
struct prg_t *prg;		/*!< heap-allocated by tapclean_init() */
#else
struct blk_t *blk[BLKMAX];	/*!< Database of all found entities */
struct prg_t prg[BLKMAX];	/*!< Database of all extracted files (prg's) */
#endif
int database_is_full = FALSE;	/*!< Flag that indicates database capacity
				     reached */

/**
 *	Allocate ram to file database and initialize array pointers.
 *
 *	@param void
 *
 *	@return TRUE on success
 *	@return FALSE on memory allocation failure
 */

int database_create_blk_db(void)
{
	int i;

	for (i = 0; i < BLKMAX; i++) {
		blk[i] = (struct blk_t*)malloc(sizeof(struct blk_t));
		if (blk[i] == NULL) {
			printf("\nError: malloc failure whilst creating file database.");
			database_destroy_blk_db(); /* Free any already allocated resource */
			return FALSE;
		}

		blk[i]->dd = NULL;
		blk[i]->fn = NULL;
	}

	return TRUE;
}

/**
 *	Clear database
 *
 *	@param void
 *
 *	@return none
 */

void database_reset_blk_db(void)
{
	int i;

	for (i = 0 ; i < BLKMAX; i++) {		/*!< clear database... */
		blk[i]->lt = LT_NONE;
		blk[i]->p1 = 0;
		blk[i]->p2 = 0;
		blk[i]->p3 = 0;
		blk[i]->p4 = 0;
		blk[i]->xi = 0;
		blk[i]->meta1 = 0;
		blk[i]->cs = 0;
		blk[i]->ce = 0;
		blk[i]->cx = 0;
		blk[i]->crc = 0;
		blk[i]->rd_err = 0;
		blk[i]->cs_exp = HASNOTCHECKSUM;
		blk[i]->cs_act = HASNOTCHECKSUM;
		blk[i]->pilot_len = 0;
		blk[i]->trail_len = 0;
		blk[i]->ok = 0;

		if (blk[i]->dd != NULL) {
			free(blk[i]->dd);
			blk[i]->dd = NULL;
		}

		if(blk[i]->fn != NULL) {
			free(blk[i]->fn);
			blk[i]->fn = NULL;
		}
	}
}

/**
 *	Add a block definition (file details) to the database (blk)
 *
 *	Only sof & eof must be assigned (legal) values for the block,
 *	the others can be 0.
 *
 *	@param lt loader id
 *	@param sof start of block offset
 *	@param sod start of data offset
 *	@param eod end of data offset
 *	@param eof end of block offset
 *	@param xi extra info
 *	@param meta1 meta info 1
 *
 *	@return Slot number the block went to (value is >= 0)
 *	@return DBERR on invalid block definition (due to overlap)
 *	@return DBFULL on database full
 */

int database_add_blk_def_ex(int lt, int sof, int sod, int eod, int eof, int xi, int meta1)
{
	int i, slot, e1, e2;

	if (debug == FALSE) {

		/* check that the block does not conflict with any existing blocks... */

		for (i = 0; blk[i]->lt != LT_NONE; i++) {
			e1 = blk[i]->p1;	/* get existing block start pos  */
			e2 = blk[i]->p4;	/* get existing block end pos   */

			if (!((sof < e1 && eof < e1) || (sof > e2 && eof > e2)))
				return DBERR;
		}
	}

	if ((sof > 19 && eof < tap.len) && (eof >= sof)) {

		/* find the first free slot (containing 0 in 'lt' field)... */

		/* note: slot blk[BLKMAX-1] is reserved for the list terminator. */
		/* the last usable slot is therefore BLKMAX-2. */

		for (i = 0; blk[i]->lt != LT_NONE; i++);

		slot = i;

		if (slot == BLKMAX-1) {	/* only clear slot is the last one? (the terminator) */
			if (database_is_full == FALSE) {	/* we only need give the error once */
				if (!batchmode)		/* dont bother with the warning in batch mode.. */
					msgout("\n\nWarning: FT's database is full...\nthe report will not be complete.\nTry optimizing.\n\n");
				database_is_full = TRUE;
			}
			return DBFULL;
		} else {

			/* put the block in the last available slot... */

			blk[slot]->lt = lt;
			blk[slot]->p1 = sof;
			blk[slot]->p2 = sod;
			blk[slot]->p3 = eod;
			blk[slot]->p4 = eof;
			blk[slot]->xi = xi;
			blk[slot]->meta1 = meta1;

			/* just clear out the remaining fields... */

			blk[slot]->cs = 0;
			blk[slot]->ce = 0;
			blk[slot]->cx = 0;
			blk[slot]->dd = NULL;
			blk[slot]->crc = 0;
			blk[slot]->rd_err = 0;
			blk[slot]->cs_exp = HASNOTCHECKSUM;
			blk[slot]->cs_act = HASNOTCHECKSUM;
			blk[slot]->pilot_len = 0;
			blk[slot]->trail_len = 0;
			blk[slot]->fn = NULL;
			blk[slot]->ok = 0;
		}
	} else {
		return DBERR;
	}

#ifdef TAPCLEAN_EMBEDDED
	/* Early scan exit: once found entities account for nearly the whole
	   tape, the remaining ~85 scanners cannot add anything meaningful.
	   Set 'aborted' so search_tap() skips them (every scanner call is
	   gated on !aborted); the scanner currently running finishes its
	   pass. Only checked when a data block was added, so an all-pause
	   tape still gets the full sweep. tapclean_analyze_tap() clears
	   'aborted' before each scan. */
	if (tap.len > 20 && lt > PAUSE) {
		long long tot = database_count_recognized_pulses();
		if (tot * 100 >= (long long)(tap.len - 20) * 97)
			aborted = TRUE;
	}
#endif

	return slot;	/* ok, entry added successfully.   */
}

/**
 *	Add a block definition (file details) to the database (blk)
 *
 *	Only sof & eof must be assigned (legal) values for the block,
 *	the others can be 0.
 *
 *	@param lt loader id
 *	@param sof start of block offset
 *	@param sod start of data offset
 *	@param eod end of data offset
 *	@param eof end of block offset
 *	@param xi extra info
 *
 *	@return Slot number the block went to (value is >= 0)
 *	@return DBERR on invalid block definition (due to overlap)
 *	@return DBFULL on database full
 */

int database_add_blk_def(int lt, int sof, int sod, int eod, int eof, int xi)
{
	return database_add_blk_def_ex(lt, sof, sod, eod, eof, xi, 0);
}

/**
 *	Sort the database by p1 (file start position, sof) values.
 *
 *	@param void
 *
 *	@return none
 */

void database_sort_blks(void)
{
	int i, swaps,size;
	struct blk_t *tmp;

	for (i = 0; blk[i]->lt != LT_NONE && i < BLKMAX; i++);

	size = i;	/* store current size of database */

	do {
		swaps = 0;
		for (i = 0; i < size - 1; i++) {

			/* examine file sof's (p1's), swap if necessary... */

			if ((blk[i]->p1) > (blk[i + 1]->p1)) {
				tmp = blk[i];
				blk[i] = blk[i + 1];
				blk[i + 1] = tmp;
				swaps++;
			}
		}
	} while(swaps != 0);	/* repeat til no swaps occur.  */
}

/*
 * Searches the file database for gaps. adds a definition for any found.
 * Note: Must be called ONLY after sorting the database!.
 * Note : The database MUST be re-sorted after a GAP is added!.
 */

void database_scan_gaps(void)
{
	int i, p1, p2, sz;

	p1 = 20;			/* choose start of TAP and 1st block's first pulse  */
	p2 = blk[0]->p1;

	if (p1 < p2) {
		sz = p2 - p1;
		database_add_blk_def(GAP, p1, 0, 0, p2 - 1, sz);
		database_sort_blks();
	}

	/* double dragon sticks in this loop */

	for (i = 0; blk[i]->lt != LT_NONE && blk[i + 1]->lt != LT_NONE; i++) {
		p1 = blk[i]->p4;		/* get end of this block */
		p2 = blk[i + 1]->p1;		/* and start of next  */
		if (p1 < (p2 - 1)) {
			sz = (p2 - 1) - p1;
			if (sz > 0) {
				database_add_blk_def(GAP, p1 + 1, 0, 0, p2 - 1, sz);
				database_sort_blks();
			}
		}
	}

	p1 = blk[i]->p4;		/* choose last blocks last pulse and End of TAP */
	p2 = tap.len - 1;
	if (p1 < p2) {
		sz = p2 - p1;
		database_add_blk_def(GAP, p1 + 1, 0, 0, p2, sz);
		database_sort_blks();
	}
}

/*
 * Counts the number of standard CBM boot sequence(s) (HEAD,HEAD,DATA,DATA) in
 * the current file database.
 * Returns the number found.
 */

int database_count_bootparts(void)
{
	int tblk[BLKMAX];
	int i, j, bootparts;

	/* make a block list (types only) without gaps/pauses included.. */

	for (i = 0,j = 0; blk[i]->lt != LT_NONE; i++) {
		if (blk[i]->lt > PAUSE)
			tblk[j++] = blk[i]->lt;
	}

	tblk[j] = 0;

	/* count bootable sections... */

	bootparts =0;

	for (i = 0; i + 3 < j; i++) {
		if (tblk[i + 0] == CBM_HEAD && tblk[i + 1] == CBM_HEAD && tblk[i + 2] == CBM_DATA && tblk[i + 3] == CBM_DATA)
			bootparts++;
	}

	return bootparts;
}

/*
 * Returns the number of imperfect pulsewidths found in the file at database
 * entry 'slot'.
 */

int database_count_unopt_pulses(int slot)
{
	int i, t, b, imperfect;
	int s, e;

	t = blk[slot]->lt;

	s = blk[slot]->p1;
	e = blk[slot]->p4;

	imperfect = 0;

	/* Trailer uses different pulsewidths, so do not consider it unoptimized
	   according to the block pulsewidths */
	if (t == ACTIONREPLAY_STURBO) {
		for (i = blk[slot]->p3 + 8; i <  blk[slot]->p4 + 1; i++) {
			b = tap.tmem[i];
			if (b != ft[ACTIONREPLAY_TURBO].sp && b != ft[ACTIONREPLAY_TURBO].lp)
				imperfect++;
		}

		e = blk[slot]->p3;		/* move block end backwards */
	}

	for(i = s; i < e + 1; i++) {
		b = tap.tmem[i];
		if (b != ft[t].sp && b != ft[t].mp && b != ft[t].lp)
			imperfect++;
	}

	return imperfect;
}

/*
 * Return a count of files in the database that are 100% optimized...
 */

int database_count_opt_blks(void)
{
	int i, n;

	for (n = 0, i = 0; blk[i]->lt != LT_NONE; i++) {
		if (blk[i]->lt > PAUSE) {
			if (database_count_unopt_pulses(i) == 0)
				n++;
		}
	}

	return n;
}

/*
 * Counts pauses within the tap file, note: v1 pauses are still held
 * as separate entities in the database even when they run consecutively.
 * This function counts consecutive v1's as a single pause.
 */

int database_count_pauses(void)
{
	int i, n;

	for (n = 0, i = 0; blk[i]->lt != LT_NONE; i++) {
		if (blk[i]->lt == PAUSE) {
			n++;
			if (tap.version == 1) {		/* consecutive v1 pauses count as 1 pause.. */
				while (blk[i++]->lt == PAUSE && i < BLKMAX - 1);
				i--;
			}
		}
	}

	return n;
}

/*
 * Return the number of pulses accounted for in total across all known files.
 */

int database_count_recognized_pulses(void)
{
	int i, tot;

	/* add up number of pulses accounted for... */

	/* for each block entry in blk */

	for (i = 0, tot = 0; blk[i]->lt != LT_NONE; i++) {
		if (blk[i]->lt != GAP) {

			/* start and end addresses both present?  */

			if (blk[i]->p1 != 0 && blk[i]->p4 != 0)
				tot += (blk[i]->p4 - blk[i]->p1) + 1;
		}
	}

	return tot;
}

/*
 * Returns the quantity of 'has checksum and its OK' files in the database.
 */

int database_count_good_checkbytes(void)
{
	int i, c;

	for (i = 0,c = 0; blk[i]->lt != LT_NONE; i++) {
		if (blk[i]->cs_exp != HASNOTCHECKSUM) {
			if (blk[i]->cs_exp == blk[i]->cs_act)
				c++;
		}
	}

	return c;
}

/*
 * Add together all data file CRC32's
 */

int database_compute_overall_crc(void)
{
	int i, tot = 0;

	for (i = 0; blk[i]->lt != LT_NONE; i++)
		tot += blk[i]->crc;

	return tot;
}

/**
 *	Dump database contents to console (for debug purposes)
 *
 *	@param void
 *
 *	@return none
 */

void database_dump_blk_db(void)
{
	int i, t;

	for (i = 0; i < BLKMAX && blk[i]->lt != LT_NONE; i++) {
		t = blk[i]->lt;		/* get block type */
		sprintf(lin, "\nName: %s ($%X-$%X)", ft[t].name[0] ? ft[t].name : "NULL", blk[i]->p1, blk[i]->p4);
		msgout(lin);
	}
}

/**
 *	Deallocate file database from RAM
 *
 *	A check for non-NULL is done, in case we are freeing resources
 *	after a malloc failure in database_create_blk_db() (clean job).
 *
 *	@param void
 *
 *	@return none
 */

void database_destroy_blk_db(void)
{
	int i;

	for (i = 0; i < BLKMAX && blk[i] != NULL; i++)
		free(blk[i]);
}



/* Still unverified PRG functions*/

/*
 * Create a table of exportable PRGs in the prg[] array based on the current
 * data extractions available in the blk[] array.
 * Note: if 'prgunite' is not 0, then any neigbouring files will be connected
 * as a single PRG. (neighbour = data addresses run consecutively).
 */

void database_make_prg_db(void)
{
#ifdef TAPCLEAN_EMBEDDED
	/* BLKMAX ints are too much stack for an embedded task */
	int i, c, j, t, x, s, e, errors, ti, *pt;
	unsigned char *tmp, done;

	pt = (int*)malloc((BLKMAX + 1) * sizeof(int));
	if (pt == NULL)
		return;
#else
	int i, c, j, t, x, s, e, errors, ti, pt[BLKMAX];
	unsigned char *tmp, done;
#endif

	/* Create table of all exported files by index (of blk)...
	 * It is used to check if next file is a neighbour without having
	 * to scan ahead for next blk with data in it
	 */
	for (i = 0,j = 0; blk[i]->lt != LT_NONE; i++) {
		if (blk[i]->dd != NULL)
			pt[j++] = i;
	}
	pt[j] = -1;			/* terminator */

	/* Clear the prg table... */
	database_reset_prg_db();

	tmp = (unsigned char*)malloc(65536 * 2);	/* create buffer for unifications */
	j = 0;						/* j steps through the finished prg's */

	/* scan through the 'data holding' blk[] indexes held in pt[]... */

	for (i = 0; pt[i] != -1 ;i++) {
		if (blk[pt[i]]->dd != NULL) {	/* should always be true. */
			ti = 0;
			done = 0;

			s = blk[pt[i]]->cs;	/* keep 1st start address. */
			errors = 0;		/* this will count the errors found in each blk */
						/* entry used to create the final data prg in tmp. */

			prg[j].blkidstart = pt[i];

			if (blk[pt[i]]->fn) {
				prg[j].fn = (char *) malloc (strlen(blk[pt[i]]->fn) + 1);

				if (prg[j].fn) {
					strcpy (prg[j].fn, blk[pt[i]]->fn);
					fname_text(prg[j].fn);
				}
			}

			do {
				t = blk[pt[i]]->lt;	/* get details of next exportable part... */
							/* note: where files are united, the type will */
							/* be set to the type of only the last file */
				x = blk[pt[i]]->cx;
				e = blk[pt[i]]->ce;
				errors += blk[pt[i]]->rd_err;

				/* bad checksum also counts as an error */

				if (ft[blk[pt[i]]->lt].has_cs == TRUE && blk[pt[i]]->cs_exp != blk[pt[i]]->cs_act)
					errors++;

				for (c = 0; c < x; c++)
					tmp[ti++] = blk[pt[i]]->dd[c];	/* copy the data to tmp buffer */

				/* block unification wanted?... */

				if (prgunite) {

					/* scan following blocks and override default details. */

					if (pt[i + 1] != -1) {		/* another file available? */
						if (blk[pt[i + 1]]->cs == (e + 1))	/* is next file a neighbour? */
							i++;
						else
							done = 1;
					} else
						done =1;
				} else
					done =1;
			} while(!done);

#ifdef TAPCLEAN_EMBEDDED
			prg[j].blkidend = pt[i];
#endif

			/* create the finished prg entry using data in tmp...  */

			x = ti;					/* set final data length */
			prg[j].dd = (unsigned char*)malloc(x);	/* allocate the ram */
			for (c = 0; c < x; c++)			/* copy the data.. */
				prg[j].dd[c] = tmp[c];
			prg[j].lt = t;				/* set file type */
			prg[j].cs = s;				/* set file start address */
			prg[j].ce = e;				/* set file end address */
			prg[j].cx = x;				/* set file length */
			prg[j].errors = errors;			/* set file errors */
			j++;					/* onto the next prg... */
		}
	}

	prg[j].lt = 0;		/* terminator */

	free(tmp);
#ifdef TAPCLEAN_EMBEDDED
	free(pt);
#endif
}

/*
 * Save all available prg's to a folder (console app only).
 * Returns the number of files saved.
 */

int database_save_prg_db(void)
{
#ifdef TAPCLEAN_EMBEDDED
	/* Saving PRGs to a 'prg' folder next to the executable makes no
	   sense on the device; the API consumer reads prg[] directly. */
	return 0;
#else
	int i;
	FILE *fp;

	chdir(exedir);

	if (chdir("prg") == 0) {	/* delete old prg's if exist... */
#ifdef WIN32
		intptr_t dirp;
		struct _finddata_t dp;

		dirp = _findfirst("*.*", &dp);
		if (dirp != -1) {
			do {
				if (strncmp(dp.name, ".", 1))
					unlink(dp.name);
			} while(_findnext(dirp, &dp) == 0);
		}
#else
		DIR *dirp;

		dirp = opendir(".");
		if (dirp != NULL) {
			struct dirent *dp;

			while ((dp = readdir(dirp)) != NULL)
				if (strncmp(dp->d_name, ".", 1))
					unlink (dp->d_name);
			closedir(dirp);
		}
#endif
	} else {
#ifdef WIN32
		mkdir("prg");
#else
		mkdir("prg", 0755);
#endif
		chdir("prg");
	}

	for (i = 0; prg[i].lt != 0; i++) {
		sprintf(lin, "%03d (%04X-%04X)", prg[i].blkidstart + 1, prg[i].cs, prg[i].ce);
		if (prg[i].fn != NULL) {
			strcat(lin, " [");
			strcat(lin, prg[i].fn);
			strcat(lin, "]");
		}
		if (prg[i].errors != 0)		/* append error indicator (if necessary) */
			strcat(lin, " BAD");
		strcat(lin, ".prg");

#ifdef WIN32
		fp = fopen(lin, "w+b");
#else
		fp = fopen(lin, "w+");
#endif
		if (fp != NULL) {
			fputc(prg[i].cs & 0xFF, fp);		/* write low byte of start address */
			fputc((prg[i].cs & 0xFF00) >> 8, fp);	/* write high byte of start address */
			fwrite(prg[i].dd, prg[i].cx, 1, fp);	/* write data */
			fclose(fp);
		}
	}

	chdir(exedir);
	return 0;
#endif /* !TAPCLEAN_EMBEDDED */
}

/* empty the prg datbase...  */
void database_reset_prg_db(void)
{
	int i;

	for (i = 0; i < BLKMAX; i++) {		/* empty the prg datbase...  */
		prg[i].lt = 0;
		prg[i].cs = 0;
		prg[i].ce = 0;
		prg[i].cx = 0;
		if (prg[i].fn != NULL) {
			free(prg[i].fn);
			prg[i].fn = NULL;
		}
		if (prg[i].dd != NULL) {
			free(prg[i].dd);
			prg[i].dd = NULL;
		}

		prg[i].errors = 0;
	}
}
