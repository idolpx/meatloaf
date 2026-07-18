/*
 * skewadapt.c (Skewed pulse adapting bit reader)
 *
 * Kevin Palberg, July 2009.
 *
 * TODO:
 * - Better error detection. Currently the only error condition is that
 *   the read pulse is exactly on the threshold.
 * - Add an option to the generic readttbit() to branch here only for
 *   tap positions that are after CBM data?
 *
 * Parts of code from main.c readttbit() function.
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


#include "main.h"
#include "mydefs.h"


/* Global variable that can be used to enable/disable skew adapting without
 * affecting the skewadapt command line option variable.
 */
char skewadapt_enabled = FALSE;


/*
 * The pulse cache consists of three separate circular buffers. The three
 * parts are for bit 0, bit 1 and read error pulses. For example, the
 * bit 0 part will contain CACHE_BIT0_SIZE bit 0 pulses before the currently
 * read tap position.
 *
 * The CACHE_ERROR_SIZE also sets the error tolerance. The cache is erased
 * whenever the error cache gets full and caching is restarted after the
 * causing position.
 */

#define CACHE_BIT0_SIZE		32
#define CACHE_BIT1_SIZE		32
#define CACHE_ERROR_SIZE	4

#define CACHE_SIZE		(CACHE_BIT0_SIZE + \
				 CACHE_BIT1_SIZE + \
				 CACHE_ERROR_SIZE)

typedef struct {
	unsigned char width;	/* pulse tap value */
	unsigned char value;	/* 0 = short (bit 0) pulse, 1 = long (bit 1) */
	int pos;		/* pulse position in tap. 0 = free slot. */
} cache_elem_t;

static cache_elem_t cache[CACHE_SIZE];
static cache_elem_t *cache_bit0 = &cache[0];
static cache_elem_t *cache_bit1 = &cache[CACHE_BIT0_SIZE];
static cache_elem_t *cache_err = &cache[CACHE_BIT0_SIZE + CACHE_BIT1_SIZE];

static int next_bit0;		/* next bit 0 cache index to get used */
static int next_bit1;		/* next bit 1 cache index to get used */
static int next_err;		/* next error cache index to get used */

static int cache_max_pos;	/* The largest cached tap position */

/*
 * The reset position circular buffer. It is for optimization purposes.
 */

#define MAX_RESET_POS		16
static int reset_pos[MAX_RESET_POS];
static int reset_pos_start;	/* Starting index of the buffer */
static int num_reset_pos;	/* Number of valid entries */


static int cache_lp;
static int cache_sp;
static int cache_tp;


/*
 * Initialize pulse cache. The first part is for bit 0 pulses, the second
 * for bit 1 pulses and the last part is for read errors.
 */

static void init_pulse_cache(void)
{
	int i;

	for (i = 0; i < CACHE_BIT0_SIZE; i++) {
		cache_bit0[i].value = 0;
		cache_bit0[i].pos = 0;
	}

	for (i = 0; i < CACHE_BIT1_SIZE; i++) {
		cache_bit1[i].value = 1;
		cache_bit1[i].pos = 0;
	}

	for (i = 0; i < CACHE_ERROR_SIZE; i++) {
		cache_err[i].value = -1;
		cache_err[i].pos = 0;
	}

	next_bit0 = 0;
	next_bit1 = 0;
	next_err = 0;

	cache_max_pos = 0;
	num_reset_pos = 0;
}

/*
 * Erase all pulse cache entries.
 */

static void erase_pulse_cache(void)
{
	int i;

	for (i = 0; i < CACHE_SIZE; i++)
		cache[i].pos = 0;

	cache_max_pos = 0;
}

/*
 * Read one pulse from the tap file offset at 'pos', decide whether it is a
 * Bit0 or Bit1 according to the values in the parameters.
 * lp = ideal long pulse width.
 * sp = ideal short pulse width.
 * tp = threshold pulse width
 *
 * The threshold pulse width is required. Caller must ensure that all of
 * lp, sp and tp are valid.
 *
 * Return (bit value) 0 or 1 on success, else -1 on failure.
 *
 * The threshold pulse width is automatically adjusted based on the average
 * value of previously read pulses. The normal generic readttbit() function
 * may fail if the pulse lengths drift (stretch out) too much over time.
 * The present function keeps track of the drift provided it's smooth enough.
 *
 * The pulse cache always contains the same pulses after a call with
 * a specific set of pos, lp, sp and tp. It doesn't matter if the caller
 * skips pulses forward or goes backward. The cache is updated appropriately
 * in both cases and the state will be the same as if bits were sequentially
 * read starting from tap position 20. This is done so that the search()
 * describe() functions in scanners get the pulses interpreted the same.
 */

int skewadapt_readttbit(int pos, int lp, int sp, int tp)
{
	int valid, v, b;
	int skew = 0;
	int ssum = 0, lsum = 0;
	int ns = 0, nl = 0;
	int i;

	if (pos < 20 || pos > tap.len - 1)	/* return error if out of bounds.. */
		return -1;

	if (is_pause_param(pos))		/* return error if offset is on a pause.. */
		return -1;

	/* Initialize pulse cache for a new combination of lp, sp, tp */

	if (cache_lp != lp ||
	    cache_sp != sp ||
	    cache_tp != tp) {

		init_pulse_cache();

		cache_lp = lp;
		cache_sp = sp;
		cache_tp = tp;
	}

	/* Check if the current tap file position is in the cache and, if so,
	 * return the decoded bit value. If the requested position preceeds
	 * anything in the cache, the cache is erased (it'll be rebuilt
	 * below.)
	 */

	if (pos <= cache_max_pos) {
		for (i = 0; i < CACHE_SIZE; i++) {
			if (cache[i].pos == pos)
				return cache[i].value;
		}

		erase_pulse_cache();
	}

	/* If the requested tap position is not the last cached position + 1,
	 * read all the pulses in between to get the cache up to date.
	 */

	if (cache_max_pos + 1 < pos) {
		if (cache_max_pos > 0) {
			/* Cache not empty. Start reading from the last
			 * cached position + 1.
			 */
			i = cache_max_pos + 1;
		} else {
			/* Cache empty. Start reading from the highest
			 * possible stored reset point. The reading could
			 * always start from 20; this stuff is an
			 * optimization.
			 */
			for (i = 0; i < num_reset_pos; i++) {
				if (reset_pos[(reset_pos_start + i) %
					      MAX_RESET_POS] >= pos)
					break;
			}

			num_reset_pos = i;
			if (i == 0)
				i = 20;
			else
				i = reset_pos[(reset_pos_start + i - 1) %
					      MAX_RESET_POS] + 1;
		}

		/* Read pulses to update the cache. Return values are not
		 * useful here.
		 */
		for (; i < pos; i++)
			skewadapt_readttbit(i, lp, sp, tp);
	}

	/* Sum all bit 0 tap pulse widths, and do the same for bit 1 pulses. */

	for (i = 0; i < CACHE_BIT0_SIZE + CACHE_BIT1_SIZE; i++) {
		if (cache[i].pos == 0)
			break;

		if (cache[i].value == 0) {
			ssum += cache[i].width;
			ns++;
		} else {
			lsum += cache[i].width;
			nl++;
		}
	}

	/* Calculate threshold skew if the cache has full bit 0 and bit 1
	 * entries. Currently this actually just amounts to setting the
	 * threshold to the average of all pulse widths in the cache.
	 */

	if (i == CACHE_BIT0_SIZE + CACHE_BIT1_SIZE) {
/* 		sskew = ssum/ns - sp; */
/* 		lskew = lsum/nl - lp; */
/* 		skew = (sskew + lskew)/2; */
		skew = (ssum/ns + lsum/nl)/2 - tp;
	}

	/* Read the tap pulse width in the requested position and determine
	 * if it represents bit 0 or bit 1.
	 */

	valid = 0;
	b = tap.tmem[pos];

	if (b < tp + skew && b > 0) {	/* its a SHORT (Bit0) pulse... */
		v = 0;
		valid += 1;
	}

	if (b > tp + skew) {		/* its a LONG (Bit1) pulse... */
		v = 1;
		valid += 2;
	}

	if (b == tp + skew)		/* its ON the threshold!... */
		valid += 4;

	if (valid == 0) {		/* Error, pulse didnt qualify as either Bit0 or Bit1... */
		add_read_error(pos);
		v = -1;
	}

	if (valid == 4) {		/* Error, pulse is ON the threshold... */
		add_read_error(pos);
		v = -1;
	}

	/* Add the new pulse to cache */

	if (v == 0) {
		cache_bit0[next_bit0].width = b;
		cache_bit0[next_bit0].pos = pos;
		next_bit0 = (next_bit0 + 1) % CACHE_BIT0_SIZE;
		cache_max_pos = pos;
	} else if (v == 1) {
		cache_bit1[next_bit1].width = b;
		cache_bit1[next_bit1].pos = pos;
		next_bit1 = (next_bit1 + 1) % CACHE_BIT1_SIZE;
		cache_max_pos = pos;
	} else {
		if (cache_err[next_err].pos == 0) {
			/* Free space available in the error cache */
			cache_err[next_err].pos = pos;
			next_err = (next_err + 1) % CACHE_ERROR_SIZE;
			cache_max_pos = pos;
		} else {
			/* Error cache full. Maximum error tolerance (error
			 * density) has been reached, so reset the cache and
			 * store the reset causing position in a table. This
			 * table is used for optimization purposes.
			 */

			erase_pulse_cache();

			if (num_reset_pos == MAX_RESET_POS) {
				reset_pos_start = (reset_pos_start + 1) %
					MAX_RESET_POS;
				num_reset_pos--;
			}

			reset_pos[(reset_pos_start + num_reset_pos) %
				  MAX_RESET_POS] = pos;
			num_reset_pos++;
		}
	}

	/* Delete the least entry in the error cache if it's position is
	 * before any bit 0 and bit 1 entries in the cache. This is so
	 * that old errors don't linger around.
	 */

	{
		int *err = &cache_err[next_err].pos;
		int *bit0 = &cache_bit0[next_bit0].pos;
		int *bit1 = &cache_bit1[next_bit1].pos;

		if (*err > 0 && *err < *bit0 && *err < *bit1) {
			*err = 0;
		}
	}

	return v;
}
