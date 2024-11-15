// /*
//     NIBREAD - part of the NIBTOOLS package for 1541/1571 disk image nibbling
//    	by Peter Rittwage <peter(at)rittwage(dot)com>
//     based on code from MNIB, by Dr. Markus Brenner
// */

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <signal.h>
// #include <time.h>
// #include <ctype.h>

// #include "mnibarch.h"
// #include "gcr.h"
// //#include "nibtools.h"
// #include "lz.h"

// int _dowildcard = 1;

// char bitrate_range[4] = { 43 * 2, 31 * 2, 25 * 2, 18 * 2 };
// char bitrate_value[4] = { 0x00, 0x20, 0x40, 0x60 };
// char density_branch[4] = { 0xb1, 0xb5, 0xb7, 0xb9 };

// BYTE compressed_buffer[(MAX_HALFTRACKS_1541 + 2) * NIB_TRACK_LENGTH];
// BYTE file_buffer[(MAX_HALFTRACKS_1541 + 2) * NIB_TRACK_LENGTH];
// BYTE track_buffer[(MAX_HALFTRACKS_1541 + 2) * NIB_TRACK_LENGTH];
// BYTE track_density[MAX_HALFTRACKS_1541 + 2];
// BYTE track_alignment[MAX_HALFTRACKS_1541 + 2];
// size_t track_length[MAX_HALFTRACKS_1541 + 2];

// size_t error_retries;
// int file_buffer_size;
// int reduce_sync, reduce_badgcr, reduce_gap;
// int fix_gcr;
// int start_track, end_track, track_inc;
// int read_killer;
// int align;
// int drivetype;
// int imagetype;
// int mode;
// int force_density;
// int track_match;
// int gap_match_length;
// int cap_min_ignore;
// int interactive_mode;
// int verbose;
// int extended_parallel_test;
// int force_nosync;
// int ihs;
// int auto_capacity_adjust;
// int align_disk;
// int skew;
// int rawmode;
// int rpm_real;
// int unformat_passes;
// int capacity_margin;
// int align_delay;
// int align_report;
// int increase_sync = 0;
// int presync = 0;
// BYTE fillbyte = 0x55;
// BYTE drive = 8;
// char * cbm_adapter = "";
// int use_floppycode_srq = 0;
// int override_srq = 0;
// int extra_capacity_margin=5;
// int sync_align_buffer=0;
// int fattrack=0;
// int old_g64=0;
// int backwards=0;
// int nb2cycle=0;

// BYTE density_map;
// float motor_speed;

// CBM_FILE fd;
// FILE *fplog;

// int ARCH_MAINDECL
// main(int argc, char *argv[])
// {
// 	int bump, reset, i;
// 	double st, et;
// 	char filename[256], logfilename[256], *dotpos;
// 	char argcache[256];
// 	FILE *fp;

// 	printf(
// 		"nibread - Commodore 1541/1571 disk image nibbler\n"
// 		AUTHOR VERSION "\n");

// 	/* we can do nothing with no switches */
// 	if (argc < 2)
// 		usage();

// 	/* clear heap buffers */
// 	memset(compressed_buffer, 0x00, sizeof(compressed_buffer));
// 	memset(file_buffer, 0x00, sizeof(file_buffer));
// 	memset(track_buffer, 0x00, sizeof(track_buffer));

// #ifdef DJGPP
// 	fd = 1;
// #endif

// 	bump = 1;  /* failing to bump sometimes give us wrong tracks on heavily protected disks */
// 	reset = 1;

// 	start_track = 1 * 2;
// 	end_track = 41 * 2;
// 	track_inc = 2;

// 	reduce_sync = 4;
// 	reduce_badgcr = 0;
// 	reduce_gap = 0;
// 	fix_gcr = 0;
// 	read_killer = 1;
// 	error_retries = 10;
// 	force_density = 0;
// 	track_match = 0;
// 	interactive_mode = 0;
// 	verbose = 0;
// 	extended_parallel_test = 0;
// 	force_nosync = 0;
// 	align = ALIGN_NONE;
// 	gap_match_length = 7;
// 	cap_min_ignore = 0;
// 	ihs = 0;
// 	mode = MODE_READ_DISK;
// 	align_report = 0;

// 	// cache our arguments for logfile generation
// 	strcpy(argcache, "");
// 	for (i = 0; i < argc; i++)
// 	{
// 		strcat(argcache, argv[i]);
// 		strcat(argcache," ");
// 	}

// 	// parse arguments
// 	while (--argc && (*(++argv)[0] == '-'))
// 	{
// 		switch ((*argv)[1])
// 		{
// 		case '@':
// 			cbm_adapter = &(*argv)[2];
// 			printf("* Using OpenCBM adapter %s\n", cbm_adapter);
// 			break;

// 		case 'P':
// 			printf("* Skip 1571 SRQ Support (Use parallel)\n");
// 			override_srq = 1;
// 			break;

// 		case 's':
// 			break;

// 		case 'j':
// 			printf("* 1541/1571 Index Hole Sensor (SC+ compatible)\n");
// 			Use_SCPlus_IHS = 1;
// 			use_floppycode_ihs = 1; // ihs floppy code!
// 			break;

// 		case 'x':
// 			printf("* Track Alignment Report (1541/1571 SC+ compatible IHS)\n");
// 			track_align_report = 1;
// 			use_floppycode_ihs = 1; // ihs floppy code!
// 			break;

// 		case 'y':
// 			printf("* Deep Bitrate Scan (1541/1571 SC+ compatible IHS)\n");
// 			Deep_Bitrate_SCPlus_IHS = 1;
// 			use_floppycode_ihs = 1; // ihs floppy code!
// 			break;

// 		case 'z':
// 			printf("* Testing 1541/1571 Index Hole Sensor (SC+ compatible)\n");
// 			Test_SCPlus_IHS = 1;
// 			bump = 0; // Don't bump for simple IHS check
// 			use_floppycode_ihs = 1; // ihs floppy code!
// 			break;

// 		case 'A':
// 			align_report = 1;
// 			printf("* Track Alignment Report\n");
// 			break;

// 		case 'h':
// 			track_inc = 1;
// 			end_track = 83;
// 			printf("* Using halftracks\n");
// 			break;

// 		case 'i':  // this is not implemented in SRQ mode //
// 			printf("* 1571 or SuperCard-compatible index hole sensor (use only for side 1)\n");
// 			ihs = 1;
// 			break;

// 		case 'V':
// 			track_match = 1;
// 			printf("* Enable track match (low-level verify)\n");
// 			break;

// 		case 'n':
// 			force_nosync = 1;
// 			printf("* Allowing track reads to ignore sync\n");
// 			break;

// 		case 't':
// 			extended_parallel_test = atoi(&(*argv)[2]);
// 			if(!extended_parallel_test)
// 				extended_parallel_test = 100;
// 			printf("* Extended port test loops = %d\n", extended_parallel_test);
// 			break;

// 		case 'I':
// 			interactive_mode = 1;
// 			printf("* Interactive mode\n");
// 			break;

// 		case 'd':
// 			force_density = 1;
// 			printf("* Forcing default density\n");
// 			break;

// 		case 'k':
// 			read_killer = 0;
// 			printf("* Disabling read of 'killer' tracks\n");
// 			break;

// 		case 'S':
// 			if (!(*argv)[2]) usage();
// 			st = atof(&(*argv)[2])*2;
// 			start_track = (int)st;
// 			printf("* Start track set to %.1f (%d)\n", st/2, start_track);
// 			break;

// 		case 'E':
// 			if (!(*argv)[2]) usage();
// 			et = atof(&(*argv)[2])*2;
// 			end_track = (int)et;
// 			if((et/2)>41) printf("WARNING: Most drives won't reach past 41 tracks and your head carriage can physically JAM!\n");
// 			if((et/2)>MAX_TRACKS_1541)
// 			{
// 				printf("WARNING: MAX tracks is %d\n",MAX_TRACKS_1541);
// 				end_track=(MAX_TRACKS_1541*2);
// 			}
// 			printf("* End track set to %.1f (%d)\n", et/2, end_track);
// 			break;

// 		case 'D':
// 			if (!(*argv)[2]) usage();
// 			drive = (BYTE) (atoi(&(*argv)[2]));
// 			printf("* Use Device %d\n", drive);
// 			break;

// 		case 'G':
// 			if (!(*argv)[2]) usage();
// 			gap_match_length = atoi(&(*argv)[2]);
// 			printf("* Gap match length set to %d\n", gap_match_length);
// 			break;

// 		case 'v':
// 			verbose++;
// 			printf("* Verbose on level %d\n",verbose);
// 			break;

// 		case 'e':	// change read retries
// 			if (!(*argv)[2]) usage();
// 			error_retries = atoi(&(*argv)[2]);
// 			printf("* Read retries set to %d\n", error_retries);
// 			break;

// 		case 'm':
// 			printf("* Minimum capacity ignore on\n");
// 			cap_min_ignore = 1;
// 			break;

// 		default:
// 			usage();
// 			break;
// 		}
// 	}
// 	printf("\n");

// 	if(argc < 1) usage();
// 	strcpy(filename, argv[0]);

// 	if( (fp=fopen(filename,"r")) )
// 	{
// 		fclose(fp);
// 		printf("File exists - Overwrite? (y/N)");
// 		if(getchar() != 'y') exit(0);
// 	}

// #ifdef DJGPP
// 	calibrate();
// 	if (!detect_ports(reset))
// 		return 0;
// #elif defined(OPENCBM_42)
// 	/* remain compatible with OpenCBM < 0.4.99 */
// 	if (cbm_driver_open(&fd, 0) != 0)
// 	{
// 		printf("Is your X-cable properly configured?\n");
// 		exit(0);
// 	}
// #else /* assume > 0.4.99 */
// 	if (cbm_driver_open_ex(&fd, cbm_adapter) != 0)
// 	{
// 		printf("Is your X-cable properly configured?\n");
// 		exit(0);
// 	}
// #endif

// 	/* Once the drive is accessed, we need to close out state when exiting */
// 	atexit(handle_exit);
// 	signal(SIGINT, handle_signals);

// 	if(!init_floppy(fd, drive, bump))
// 	{
// 		printf("Floppy drive initialization failed\n");
// 		exit(0);
// 	}

// 	if(extended_parallel_test)
// 		parallel_test(extended_parallel_test);

// 	if (Test_SCPlus_IHS) // "-z"
// 	{
// 		IHSresult = Check_SCPlus_IHS(fd,0); // 0=TurnIHSoffAfterwards
// 		OutputIHSResult(TRUE,FALSE,IHSresult,NULL);
// 		exit(0);
// 	}

// 	if(align_report)
// 		TrackAlignmentReport(fd);

// 	/* create log file */
// 	strcpy(logfilename, filename);
// 	dotpos = strrchr(logfilename, '.');
// 	if (dotpos != NULL)
// 		*dotpos = '\0';
// 	strcat(logfilename, ".log");

// 	if ((fplog = fopen(logfilename, "wb")) == NULL)
// 	{
// 		printf("Couldn't create log file %s!\n", logfilename);
// 		exit(2);
// 	}

// 	fprintf(fplog, "%s\n", VERSION);
// 	fprintf(fplog, "'%s'\n", argcache);

// 	if(strrchr(filename, '.') == NULL)  strcat(filename, ".nbz");

// 	if((compare_extension(filename, "D64")) || (compare_extension(filename, "G64")))
// 	{
// 		printf("\nDisk imaging only directly supports NIB, NB2, and NBZ formats.\n");
// 		printf("Use nibconv after imaging to convert to desired file type.\n");
// 		exit(0);
// 	}

// 	if (Use_SCPlus_IHS) // "-j"
// 	{
// 		IHSresult = Check_SCPlus_IHS(fd,1); // 1=KeepOn
// 		OutputIHSResult(TRUE,TRUE,IHSresult,fplog);
// 		if (IHSresult != 0)
// 		{
// 			// turn SCPlus IHS off
// 			send_mnib_cmd(fd, FL_IHS_OFF, NULL, 0);
// 			burst_read(fd);

// 			if (fplog) fclose(fplog);
// 			exit(0);
// 		}
// 	}


// 	if (Deep_Bitrate_SCPlus_IHS) // "-y"
// 	{
// 		// We need some memory for the Deep Bitrate Scan
// 		if(!(logline = malloc(0x10000)))
// 		{
// 			printf("Error: Could not allocate memory for Deep Bitrate Scan buffer.\n");
// 			exit(0);
// 		}
// 		DeepBitrateAnalysis(fd,filename,track_buffer,logline);
// 	}
// 	else if (track_align_report) // "-x"
// 		TrackAlignmentReport2(fd,track_buffer);
// 	else
// 	{
// 		if(!(disk2file(fd, filename)))
// 			printf("Operation failed!\n");
// 	}


// 	if (Use_SCPlus_IHS) // "-j"
// 	{
// 		// turn SCPlus IHS off
// 		send_mnib_cmd(fd, FL_IHS_OFF, NULL, 0);
// 		burst_read(fd);
// 	}

// 	motor_on(fd);
// 	step_to_halftrack(fd, 18*2);

// 	if(fplog) fclose(fplog);

// 	exit(0);
// }

// void parallel_test(int iterations)
// {
// 	int i;

// 	printf("Performing extended parallel port test\n");
// 	for(i=0; i<iterations; i++)
// 	{
// 		if(!verify_floppy(fd))
// 		{
// 			printf("Parallel port failed extended testing.  Check wiring and sheilding.\n");
// 			exit(0);
// 		}
// 		printf(".");
// 	}
// 	printf("\nPassed advanced parallel port test\n");
// 	exit(0);
// }

// int disk2file(CBM_FILE fd, char *filename)
// {
// 	int count = 0;
// 	char newfilename[256];
// 	char filenum[4], *dotpos;

// 	/* read data from drive to file */
// 	motor_on(fd);

// 	if(compare_extension(filename, "NB2"))
// 	{
// 		track_inc = 1;
// 		if(!(write_nb2(fd, filename))) return 0;
// 	}
// 	else if(compare_extension(filename, "NIB"))
// 	{
// 		if(!(read_floppy(fd, track_buffer, track_density, track_length))) return 0;
// 		if(!(file_buffer_size = write_nib(file_buffer, track_buffer, track_density, track_length))) return 0;
// 		if(!(save_file(filename, file_buffer, file_buffer_size))) return 0;

// 		if(interactive_mode)
// 		{
// 			for(;;)
// 			{
// 				motor_off(fd);
// 				printf("Swap disk and press a key for next image, or CTRL-C to quit.\n");
// 				getchar();
// 				motor_on(fd);

// 				/* create new filename */
// 				sprintf(filenum, "%d", ++count);
// 				strcpy(newfilename, filename);
// 				dotpos = strrchr(newfilename, '.');
// 				if (dotpos != NULL) *dotpos = '\0';
// 				strcat(newfilename, filenum);
// 				strcat(newfilename, ".nib");

// 				if(!(read_floppy(fd, track_buffer, track_density, track_length))) return 0;
// 				if(!(file_buffer_size = write_nib(file_buffer, track_buffer, track_density, track_length))) return 0;
// 				if(!(save_file(newfilename, file_buffer, file_buffer_size))) return 0;
// 			}
// 		}
// 	}
// 	else
// 	{
// 		if(!(read_floppy(fd, track_buffer, track_density, track_length))) return 0;
// 		if(!(file_buffer_size = write_nib(file_buffer, track_buffer, track_density, track_length))) return 0;
// 		if(!(file_buffer_size = LZ_CompressFast(file_buffer, compressed_buffer, file_buffer_size))) return 0;
// 		if(!(save_file(filename, compressed_buffer, file_buffer_size))) return 0;

// 		if(interactive_mode)
// 		{
// 			for(;;)
// 			{
// 				motor_off(fd);
// 				printf("Swap disk and press a key for next image, or CTRL-C to quit.\n");
// 				getchar();
// 				motor_on(fd);

// 				/* create new filename */
// 				sprintf(filenum, "%d", ++count);
// 				strcpy(newfilename, filename);
// 				dotpos = strrchr(newfilename, '.');
// 				if (dotpos != NULL) *dotpos = '\0';
// 				strcat(newfilename, filenum);
// 				strcat(newfilename, ".nbz");

// 				if(!(read_floppy(fd, track_buffer, track_density, track_length))) return 0;
// 				if(!(file_buffer_size = write_nib(file_buffer, track_buffer, track_density, track_length))) return 0;
// 				if(!(file_buffer_size = LZ_CompressFast(file_buffer, compressed_buffer, file_buffer_size))) return 0;
// 				if(!(save_file(newfilename, compressed_buffer, file_buffer_size))) return 0;
// 			}
// 		}
// 	}

// 	return 1;
// }

// void
// usage(void)
// {
// 	printf("usage: nibread [options] <filename>\n\n"
// 		 " -@x: Use OpenCBM device 'x' (xa1541, xum1541:0, xum1541:1, etc.)\n"
// 	     " -D[n]: Use drive #[n]\n"
// 	     " -e[n]: Retry reading tracks with errors [n] times\n"
// 	     " -S[n]: Override starting track\n"
// 	     " -E[n]: Override ending track\n"
// 	     " -G[n]: Match track gap by [n] number of bytes (advanced users only)\n"
// 	     " -P: Use parallel transfer instead of SRQ (1571 only)\n"
// 	     " -k: Disable reading of 'killer' tracks\n"
// 	     " -d: Force default densities\n"
// 	     " -v: Enable track matching (crude read verify)\n"
// 	     " -I: Interactive imaging mode\n"
// //	     " -m: Disable minimum capacity check\n"
// 	     " -V: Verbose (output more detailed track data)\n"
// 	     " -h: Read halftracks\n"
// 	     " -t: Extended parallel port tests\n"
// 	     " -j: Use Index Hole Sensor  (1541/1571 SC+ compatible IHS)\n"
// 	     " -x: Track Alignment Report (1541/1571 SC+ compatible IHS)\n"
// 	     " -y: Deep Bitrate Analysis  (1541/1571 SC+ compatible IHS)\n"
// 	     " -z: Test Index Hole Sensor (1541/1571 SC+ compatible IHS)\n"
// 	     );
// 	exit(1);
// }