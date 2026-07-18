/**
 *	@file 	crc32.h
 *	@brief	CRC32 calculator for PRG file contents.
 *
 *	This module is used to calculate the CRC32 of data within the PRG database.
 */

#ifndef __FINALTAPCRC32_H__
#define __FINALTAPCRC32_H__

#define CRC32_POLYNOMIAL	0xEDB88320L	/*!< CRC32 polynomial */

/**
 *	Prototypes
 */

int crc32_build_crc_table(void);
unsigned int crc32_compute_crc(unsigned char *, int);
void crc32_free_crc_table(void);

#endif /* __FINALTAPCRC32_H__ */
