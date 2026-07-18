/**
 *	@file 	dc2nconv.h
 *	@brief	Routines to convert DC2N DMP files to TAP in memory.
 *
 *	The following code converts DC2N DMP files to TAP files. It assumes
 *	tapes for a C64 (PAL) were sampled at 2MHz.
 */

#ifndef __DC2NCONV_H__
#define __DC2NCONV_H__

/**
 *	Prototypes
 */

int dc2nconv_to_tap(unsigned char *src, unsigned char *dst, int sz);

#endif /* __DC2NCONV_H__ */
