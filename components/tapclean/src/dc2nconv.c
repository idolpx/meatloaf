/**
 *	@file 	dc2nconv.c
 *	@brief	Routines to convert DC2N DMP files to TAP in memory.
 */

#include "mydefs.h"
#include "dc2nconv.h"
#include "main.h"

#include <string.h>

#define C16_TAPE_RAW_SUPPORT	/* Use Markus' extension as with MTAP */

#ifdef C16_TAPE_RAW_SUPPORT
enum {
	TAP_FORMAT_PLATFORM_C64 = 0x00,
	TAP_FORMAT_PLATFORM_VIC20,
	TAP_FORMAT_PLATFORM_C16
};

enum {
	TAP_FORMAT_VIDEO_PAL = 0x00,
	TAP_FORMAT_VIDEO_NTSC
};
#endif

struct _tap_header {
	char id_string[0x0C];

	unsigned char version;
#ifdef C16_TAPE_RAW_SUPPORT
	unsigned char platform;
	unsigned char video_standard;
	unsigned char exp;
#else
	unsigned char exp[3];
#endif

	unsigned char data_length[0x04];	/* prevent endiannes worries */
};

struct _dmp_header {
	char id_string[0x0C];

	unsigned char version;
	unsigned char platform;
	unsigned char video_standard;

	unsigned char counter_resolution;
	unsigned char counter_rate[0x04];	/* prevent endiannes worries */
};

/*
 * Write a long pulse to the buffer
 *
 * @param output_buffer	Buffer where the long pulse size is written
 * @param lp		Long pulse size in machine clock cycles/8
 *
 * @return Amount of bytes written to output buffer
 */

static int write_long_pulse(unsigned char *output_buffer, unsigned long lp)
{
	unsigned int zerot;
	int wbytes = 0;

	lp <<= 3;

	while (lp != 0) {
		if (lp >= 0x1000000)
			zerot = 0xffffff;	/* zerot =  maximum */
		else
			zerot = lp;
		lp -= zerot;

		output_buffer[0+wbytes] = 0x00;
		output_buffer[1+wbytes] = (unsigned char) (zerot & 0xFF);
		output_buffer[2+wbytes] = (unsigned char) ((zerot>>8) & 0xFF);
		output_buffer[3+wbytes] = (unsigned char) ((zerot>>16) & 0xFF);

		wbytes += 4;
	}

	return wbytes;
}

/*
 * Conversion routine to downsample from 2 MHz to TAP,
 * assuming PAL video standard and Commodore 64
 *
 * @param utime		DMP data sample
 *
 * @return Converted sample
 */
unsigned long downsample_c64_pal (unsigned long utime)
{
	// Exact PAL freq, 985248 Hz
	return (utime * 30789UL + 31250UL) / 62500UL; // PAL
}

#ifdef C16_TAPE_RAW_SUPPORT
/*
 * Conversion routine to downsample from 2 MHz to TAP
 * assuming NTSC video standard and Commodore 64
 *
 * @param utime		DMP data sample
 *
 * @return Converted sample
 */
unsigned long downsample_c64_ntsc (unsigned long utime)
{
	// NTSC freq approximated to 1022720 Hz
	return (utime * 15980UL + 15625UL) / 31250UL; // NTSC
}

/*
 * Conversion routine to downsample from 2 MHz to TAP,
 * assuming PAL video standard and VIC
 *
 * @param utime		DMP data sample
 *
 * @return Converted sample
 */
unsigned long downsample_vic_pal (unsigned long utime)
{
	// PAL freq approximated to 1108416 Hz
	return (utime * 17319UL + 15625UL) / 31250UL; // PAL
}

/*
 * Conversion routine to downsample from 2 MHz to TAP
 * assuming NTSC video standard and VIC
 *
 * @param utime		DMP data sample
 *
 * @return Converted sample
 */
unsigned long downsample_vic_ntsc (unsigned long utime)
{
	// NTSC freq approximated to 1022720 Hz
	return (utime * 15980UL + 15625UL) / 31250UL; // NTSC
}

/*
 * Conversion routine to downsample from 2 MHz to TAP,
 * assuming PAL video standard and Commodore 16
 *
 * @param utime		DMP data sample
 *
 * @return Converted sample
 */
unsigned long downsample_c16_pal (unsigned long utime)
{
	// PAL freq approximated to 886720 Hz
	return (utime * 27710UL + 31250UL) / 62500UL; // PAL
}

/*
 * Conversion routine to downsample from 2 MHz to TAP
 * assuming NTSC video standard and Commodore 16
 *
 * @param utime		DMP data sample
 *
 * @return Converted sample
 */
unsigned long downsample_c16_ntsc (unsigned long utime)
{
	// NTSC freq approximated to 894880 Hz
	return (utime * 27965UL + 31250UL) / 62500UL; // NTSC
}
#endif

/*
 * Convert DC2N format to TAP v1
 *
 * @param input_buffer	Buffer containing the DC2N dmp file
 * @param output_buffer	Buffer where the converted TAP is written
 * @param flen		Size of the DC2N DMP file
 *
 * @return Amount of useful bytes in the output buffer
 */

int dc2nconv_to_tap(unsigned char *input_buffer, unsigned char *output_buffer, int flen)
{
	int olen;
	int i;
	unsigned long utime, clockcycles, longpulse;
	unsigned long pulse;
	struct _tap_header *th;
	struct _dmp_header *dh;

	unsigned long (*downsample)(unsigned long);

	dh = (struct _dmp_header *) input_buffer;
	th = (struct _tap_header *) output_buffer;

	strncpy(th->id_string, TAP_ID_STRING, strlen(TAP_ID_STRING));

	th->version = 0x01;	/* convert to TAP v1 */

#ifdef C16_TAPE_RAW_SUPPORT
	th->platform = dh->platform;
	th->video_standard = dh->video_standard;
	th->exp = 0x00;
#else
	th->exp[0x00] = 0x00;	/* initialize exp bytes */
	th->exp[0x01] = 0x00;
	th->exp[0x02] = 0x00;
#endif

	olen = TAP_HEADER_SIZE;
	longpulse = 0;

#ifdef C16_TAPE_RAW_SUPPORT
	switch (th->platform) {
		case TAP_FORMAT_PLATFORM_C64:
			if (th->video_standard == TAP_FORMAT_VIDEO_PAL)
			        downsample = downsample_c64_pal;
			else
			        downsample = downsample_c64_ntsc;
			break;
		case TAP_FORMAT_PLATFORM_VIC20:
			if (th->video_standard == TAP_FORMAT_VIDEO_PAL)
			        downsample = downsample_vic_pal;
			else
			        downsample = downsample_vic_ntsc;
			break;
		case TAP_FORMAT_PLATFORM_C16:
			if (th->video_standard == TAP_FORMAT_VIDEO_PAL)
			        downsample = downsample_c16_pal;
			else
			        downsample = downsample_c16_ntsc;
			break;
	}

#else /* C64-TAP-RAW format */
	downsample = downsample_c64_pal;
#endif

	for (i = DC2N_HEADER_SIZE; i < flen;) {
		utime = (unsigned long) input_buffer[i++];
		utime += 256UL * (unsigned long) input_buffer[i++];

		/* downsample data from 2MHz to C64 PAL frequency */
		clockcycles = downsample(utime);

		/* divide by eight, as requested by TAP V0 format */
		pulse = (clockcycles + 4) / 8;

		/* assert minimal length */
		if (pulse == 0)
			pulse = 1;

		if (pulse < 0x100) {
			/* a pending overflow sequence ends with this pulse */
			if (longpulse) {
				longpulse += pulse;
				olen += write_long_pulse(&output_buffer[olen], longpulse);
				longpulse = 0;
			} else {
				output_buffer[olen++] = (unsigned char) pulse;
			}
		} else if (utime < 0xFFFF) {	/* not a counter overflow, just a long pulse */
			longpulse += pulse;
			olen += write_long_pulse(&output_buffer[olen], longpulse);
			longpulse = 0;
		} else	/* counter overflows get concatenated between them and any following */
			longpulse += pulse;
	}

	/* write trailing longpulse (if any) */
	if (longpulse)
		olen += write_long_pulse(&output_buffer[olen], longpulse);

	/* store TAP data size inside header */
	th->data_length[0x00] = (unsigned char) ((olen - TAP_HEADER_SIZE)  & 0xFF);
	th->data_length[0x01] = (unsigned char) (((olen - TAP_HEADER_SIZE) >> 8) & 0xFF);
	th->data_length[0x02] = (unsigned char) (((olen - TAP_HEADER_SIZE) >> 16) & 0xFF);
	th->data_length[0x03] = (unsigned char) (((olen - TAP_HEADER_SIZE) >> 24) & 0xFF);

	return olen;
}
