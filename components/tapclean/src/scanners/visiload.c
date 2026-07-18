/*---------------------------------------------------------------------------
  visiload.c

  Part of project "Final TAP". 
  
  A Commodore 64 tape remastering and data extraction utility.

  (C) 2001-2006 Stewart Wilson, Subchrist Software.
   
  
   
   This program is free software; you can redistribute it and/or modify it under 
   the terms of the GNU General Public License as published by the Free Software 
   Foundation; either version 2 of the License, or (at your option) any later 
   version.
   
   This program is distributed in the hope that it will be useful, but WITHOUT ANY 
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
   PARTICULAR PURPOSE. See the GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License along with 
   this program; if not, write to the Free Software Foundation, Inc., 51 Franklin 
   St, Fifth Floor, Boston, MA 02110-1301 USA
   
   
  Notes:-
   
  This is a slightly tricky format, no file in the chain can be found in isolation
  apart from the 1st file as each files formatting is determined by info held in
  its predecessor.

  The scanner considers the job "finished" when read errors are found in a header
  or when the offset gets beyond 'len-300'. (this could be improved!).

30/8/2002...

  Problems with certain Visiload TAPs crashing FT are cured by the
  addition of error checking during header decodes, however I have found
  Visiload TAPs that contain "pilot-less" blocks that do NOT immediately
  begin with the header!!!. It was THIS that caused many crashes, now this
  is just causing abandonment of Visiload searches early on several TAPs
  and I need to study the visiload loader itself to see how IT is
  handling these cases.
  ie. Dizzy - Ultimate Cartoon Adventure.

  Anyway, its good to know its not a pulsewidth problem as I first
  suspected.


11/2/2002...

 Often the search is abandoned because unexpected pauses are encountered,
 I fixed today, "the captive", it contained many pauses where there should
 have been LONG pulses.

  
visiload thresholds...

t1. CRC $001CDFBE  :  threshold = $1B6 (TAP byte $35)
t2. CRC $001CE3DE  :  threshold = $1E6 (TAP byte $3B)
t3. CRC $001CE56A  :  threshold = $1F8 (TAP byte $3E)
t4. CRC $001CD5F7  :  threshold = $243 (TAP byte $47)
t5. CRC ---------  :  threshold = $291 (TAP byte $52)
t6. CRC ---------  :  threshold = $159 (TAP byte $2B)
t7. CRC ---------  :  threshold = $222 (TAP byte $44)

---------------------------------------------------------------------------*/

#include "../main.h" 
#include "../mydefs.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define HDSZ 4

/* As found in first header byte of T1/T2 modifier at the end of a chain */
#define OVERSIZED_BIT1_PULSE_T1 0x5A
#define OVERSIZED_BIT1_PULSE_T2 0x62
#define OVERSIZED_BIT1_PULSE_T6 0x45

#define VISILOAD_CBM_DATA_SIZE	0x0121

static int visi_type = VISI_T2;	/* default visiload type, overidden when loader is identified. */

/*---------------------------------------------------------------------------
 Reads a Visiload format byte at 'pos' in tap.tmem,
 'abits' specifies a number of additional (1) bits that MUST precede the byte
*/
static int visiload_readbyte(int pos, int endi, int abits, int allow_known_oversized)
{
   int b,valid;
   /*long*/ int i;
   unsigned char mb[8],p;
   
   /* check that all required bits are inside the tap and not inside any pauses... */
   for(i=0; i<(8+ abits); i++)
   {
      if(pos+i<20 || pos+i>tap.len-1)
         return -1;
      if(is_pause_param(pos+i))
         return -1;
   }

   /* read requires additional (1) bits, return -1 if they arent there. */
   for(i=0; i<abits; i++)
   {
      p = tap.tmem[pos+i];
      if(p<(ft[visi_type].lp-tol) || p>(ft[visi_type].lp+tol))  /* not valid 1 bit? */
      {
         /* 
          * If this is a known oversized bit 1 pulse in Visiload T1 where
          * allowed, fix it now
          */
         if(visi_type == VISI_T1 && 
               allow_known_oversized && 
               p>(OVERSIZED_BIT1_PULSE_T1-tol) && 
               p<(OVERSIZED_BIT1_PULSE_T1+tol))
         {
            tap.tmem[pos+i] = ft[VISI_T1].lp;
            mb[i]=1;
         }
         else if(visi_type == VISI_T2 && 
               allow_known_oversized && 
               p>(OVERSIZED_BIT1_PULSE_T2-tol) && 
               p<(OVERSIZED_BIT1_PULSE_T2+tol))
         {
            tap.tmem[pos+i] = ft[VISI_T1].lp;
            mb[i]=1;
         }
         else if(visi_type == VISI_T6 && 
               allow_known_oversized && 
               p>(OVERSIZED_BIT1_PULSE_T6-tol) && 
               p<(OVERSIZED_BIT1_PULSE_T6+tol))
         {
            tap.tmem[pos+i] = ft[VISI_T6].lp;
            mb[i]=1;
         }
         else
         {
            add_read_error(pos+i);  /* read error, visiload 'addbits' failed. */
            return -1;
         }
      }
   }

   /* read next 8 into mb[] */
   for(i=0; i<8; i++)
   {
      valid=0;

      p = tap.tmem[pos+abits+i];
      if(p>(ft[visi_type].lp-tol) && p<(ft[visi_type].lp+tol))  /* valid 1 bit? */
      {
         mb[i]=1;
         valid++;
      }
      if(p>(ft[visi_type].sp-tol) && p<(ft[visi_type].sp+tol))  /* valid 0 bit? */
      {
         mb[i]=0;
         valid++;
      }
      if(valid==0) /* not 1 or 0? */
      {
         add_read_error(pos+i);
         return -1;
      }
      if(valid==2) /* pulse qualified as 0 AND 1? */
      {
         add_read_error(pos+i);
         return -1;
      }
   }

   /* we may assume now that mb[8] contains 8 valid bits... */

   if(endi==MSbF)     /* decode them MSbF... */
   {
      b = 0;
      for(i=0; i<8; i++)
      {
         if(mb[i]==1)
            b+=(128>>i);
      }
   }
   else               /* decode them LSbF... */
   {
      b = 0;
      for(i=0; i<8; i++)
      {
         if(mb[i]==1)
            b+=(1<<i);
      }
   }
   return b;
}
/*---------------------------------------------------------------------------
*/
static void visiload_search_core(int slice_start, int slice_end, int ah, int ab, int en)
{
   int i,sof,sod,eod,eof,zcnt;
   int j,start,end,len;
   int b,att,pt,db1;
   int hd[200],hcnt;

	for (i = slice_start; i < slice_end-100; i++) {

			/* ---------------- Legacy code ---------------- */
      b= visiload_readbyte(i,en,ab,0);   /* look for a pilot byte... */
      if(b== ft[visi_type].pv)
      {
         sof= i;
         zcnt=0;
         while(visiload_readbyte(i+(zcnt*(8+ab)), en, ab, 0)==ft[visi_type].pv) /* step thru all pilot bytes.. */
            zcnt++;

         b= visiload_readbyte(i+(zcnt*(8+ab)), en, ab, 0); /* should be a sync */

         if(b==ft[visi_type].sv && zcnt>ft[visi_type].pmin)    /* got enough pilots + sync?... */
         {
            /* Application->MessageBox("at least 100 visi pilots +sync found","",MB_OK);   ok, appears just once. */

            sod= i+((zcnt+1)*(8+ab));    /* set start of data on byte after sync. */
            j= sod;
            pt=0;

            /* Found 1st block, now find all others... */
            do
            {
               if(pt==1)  /* pilot required?, read in pilot tone and sync if required... */
               {
                  do  /* find a first pilot byte... */
                  {
                     b= visiload_readbyte(j,en,ab,0);
                     j++;
                  }
                  while(b!=ft[visi_type].pv && j<slice_end-100);
                  j--;
                  sof= j;
                  do  /* read thru all pilot bytes... */
                  {
                     b= visiload_readbyte(j,en,ab,0);
                     j+=(8+ab);
                  }
                  while(b==ft[visi_type].pv && j<slice_end-100);
                  j-=(8+ab);

                  /* ! should check pilot count here?, theres usually only 2 or 3 pilots ! */

                  /* check sync byte... */
                  b= visiload_readbyte(j, en, ab, 0);
                  if(b!=ft[visi_type].sv)
                  {
                     if(!quiet)
                     {
                        sprintf(lin," * Sync byte failed @ %04X. Visiload search was aborted.", j);
                        msgout(lin);
                     }
                     return;   /* abort search if sync fails. */
                  }
                  sod= j+(8+ab);
                  pt=0;
               }

               /* pilot sync are found or none were required... */

               /* decode header... (taking into account additional header bytes) */
               for(hcnt=0; hcnt<HDSZ+ah+1; hcnt++)  /* note: +1 reads 1 data byte too. */
               {
                 /* 
                  * Allow correction of oversized bit 1 pulses in Visiload T1 header
                  * Correction is not allowed anywhere else
                  */
                  hd[hcnt]= visiload_readbyte(sod+(hcnt*(8+ab)), en, ab, 1);

                  if(hd[hcnt]==-1)   /* 27/8/2002 : this may stop the crashes */
                  {
                     if(!quiet)
                     {
                        sprintf(lin,"\nFATAL : read error in Visiload header! ($%04X).",j+(hcnt*(8+ab)));
                        msgout(lin);
                        sprintf(lin,"\nHeader begins at $%04X and should hold %d bytes (%d bits per byte).",j,HDSZ+ah,8+ab);
                        msgout(lin);

                        sprintf(lin,"\nVisiload search was aborted.");
                        msgout(lin);
                        sprintf(lin,"\nExperts note : Manual correction of this location should allow detection of more Visiload files. ");
                        msgout(lin);
                        sprintf(lin,"Typically the pulse found there will be too large to register as a Visiload LONG or the data-stream may ");
                        msgout(lin);
                        sprintf(lin,"contain an unexpected pause.");
                        msgout(lin);
                        msgout("");
                     }
                     return;
                  }
               }

               start= (hd[2+ah]<<8) + hd[3+ah];  /* start address is at offsets 2,3 */
               end= (hd[0+ah]<<8) + hd[1+ah];    /* end address is at offsets 0,1 */
               len= end-start;
               if(len==0)
                  len=1;  /* NOTE: if START==END then 1 byte will STILL be sent! */

               db1= hd[HDSZ+ah];    /* read first data byte. */
               
               eod= sod+(len+4+ah-1)*(8+ab);   /* same thing as above. */
               eof= eod+(8+ab)-1;

             /* create an attribute value for this block...
                att value...
                bit 7: endianness (1=msbf)
                bit 6: unused.
                bit 5: additional header bytes, bit 2
                bit 4: additional header bytes, bit 1
                bit 3: additional header bytes, bit 0
                bit 2: additional bits per byte, bit 2
                bit 1: additional bits per byte, bit 1
                bit 0: additional bits per byte, bit 0

                note : maximum ah and ab is 7.   */

               att= (en<<7)+(ah<<3)+ab;
               addblockdef(visi_type, sof,sod,eod,eof,att);

               j= eof+1;   /* very important to set this correctly  */
                           /* next header will be decoded immediately */
                           /* on next iteration. (unless block has pilot) */

               i= j;     /* optimize search (theres usually only 1 chain though). */
                
               /* set formatting parameters for next block... */
               if(start==0x034B)
                  ab= db1-8;   /* first data byte holds no.of bits per byte for next block */
               if(start==0x03A4)
                  ah= db1-3;   /* first data byte holds no.of add header bytes +3 for next block */

               if(start==0x0347) /* if First DATA byte is $26 then next block will be MSbF else LSbF */
               {
                  if(db1==0x26)
                     en= MSbF;
                  else
                     en= LSbF;
               }
               if(start==0x03BB) /* Next block WILL have PILOT tone before it (ANY number , can be 1 !) */
               {
                  pt=1;

                  /* Narco Police requires loader parameter reset every so often */
                  if (end == 0x03DB && !strncmp((char *) &cbm_header[5], "NARCO POLICE", 12))
                  {
                     #define NARCO_POLICE_MODIFIER_SZ 0x20

                     int data_offset;
                     unsigned char modifier_block[NARCO_POLICE_MODIFIER_SZ];
                     int m_index;
                     unsigned int crc;

                     data_offset = sod+(ah+HDSZ)*(8+ab);

                     /* We have to extract data here and check it; no other option possible */
                     for (m_index = 0; m_index < NARCO_POLICE_MODIFIER_SZ; m_index++)
                     {
                        modifier_block[m_index] = visiload_readbyte(data_offset+m_index*(8+ab), en, ab, 0);
#ifdef DEBUG
                        if (m_index % 16 ==0)
                           printf ("\n%06X: ", data_offset);
                        printf ("%02X ", modifier_block[m_index]);
#endif
                     }

                     crc = crc32_compute_crc(modifier_block, NARCO_POLICE_MODIFIER_SZ);

                     switch (crc)
                     {
                        case 0x22CA1A6A:  /* Loads the title screen */
                        case 0x7DF3B6CE:  /* Loads the menu */
                        case 0x50D31A06:
                        case 0x0AD70202:

                           en= MSbF;      /* Reset loader to use MSbF... */
                           ab= 1;         /* ... 1 extra bit per byte... */
                           ah= 0;         /* ... and no extra header bytes... */
                           break;
                     }
                  }
                  else if (end == 0x03EB && !strncmp((char *) &cbm_header[5], "PUFFY", 5))
                  {
                     #define PUFFYS_SAGA_MODIFIER_SZ 0x30

                     int data_offset;
                     unsigned char modifier_block[PUFFYS_SAGA_MODIFIER_SZ];
                     int m_index;
                     unsigned int crc;

                     data_offset = sod+(ah+HDSZ)*(8+ab);

                     /* We have to extract data here and check it; no other option possible */
                     for (m_index = 0; m_index < PUFFYS_SAGA_MODIFIER_SZ; m_index++)
                     {
                        modifier_block[m_index] = visiload_readbyte(data_offset+m_index*(8+ab), en, ab, 0);
#ifdef DEBUG
                        if (m_index % 16 ==0)
                           printf ("\n%06X: ", data_offset);
                        printf ("%02X ", modifier_block[m_index]);
#endif
                     }

                     crc = crc32_compute_crc(modifier_block, PUFFYS_SAGA_MODIFIER_SZ);

                     switch (crc)
                     {
                        case 0x82D152D1:

                           en= MSbF;      /* Reset loader to use MSbF... */
                           ab= 1;         /* ... 1 extra bit per byte... */
                           ah= 0;         /* ... and no extra header bytes... */
                           break;
                     }
                  }
               }

               sof= j;   /* set these ready for next block */
               sod= j;
            }
            while(j<slice_end-100);
         }
      }
			/* ---------------- End of legacy code ---------------- */

	}
}

void visiload_search(void)
{
	int ah, ab, en;

	int cbm_index = 1;

	if(!quiet)
		msgout("  Visiload");

	for (;;) {

		int match;
		int ib, slice_start, slice_end;
		int threshold;

		/*
		 * At this stage the describe functions have not been invoked yet,
		 * therefore we have to extract the CBM Data load address on the fly.
		 *
		 * The CBM Data load address is stored in CBM Header so we need to
		 * decode both before we can access the blk[ib]->cs record of the
		 * corresponding CBM Data.
		 *
		 * Unfortunately the load address returned for a given CBM Data
		 * might be inconsistent with what it really is, e.g. if there's a
		 * unrecognized CBM Data earlier in the tape the index of the CBM
		 * Header and that of Data get out of sync, so we use the data 
		 * block size instead, which is worked out of the Data block itself.
		 */

		match = 0;

		//find_decode_block(CBM_HEAD, cbm_index);	/* Required if we were to extract the load address */
		ib  = find_decode_block(CBM_DATA, cbm_index);
		if (ib == -1) {

			break;	/* No further titles on this tape image */

		} else if ((unsigned int) blk[ib]->cx == VISILOAD_CBM_DATA_SIZE) {

			int buf[VISILOAD_CBM_DATA_SIZE];

			int seq_endianness_and_bpb[12] = {
				0xAD, 0x0D, 0xDD, 0x4A, 0x2C, 0x0D, 0xDC, XX, 0xFC, 0x60, 0xA2, XX
			};

			int seq_header_bytes[] = {
				0xA0, XX, 0x20, XX, 0x03, 0x99, 0xAC, 0x00, 0x88, 0x10, 0xF7
			};

			int seq_pulse_threshold[] = {
				0xA9, XX, 0x8D, 0x04, 0xDD, 0xA9, XX, 0x8D, 0x05, 0xDD
			};

			int index, offset;

			/* Make an 'int' copy for use in find_seq() */
			for (index = 0; index < VISILOAD_CBM_DATA_SIZE; index++)
				buf[index] = blk[ib]->dd[index];

			/* We now look for invariants to extract loader parameters */

			match = 1;

			offset = find_seq(buf, VISILOAD_CBM_DATA_SIZE, seq_endianness_and_bpb, sizeof(seq_endianness_and_bpb) / sizeof(seq_endianness_and_bpb[0]));
			if (offset == -1) {
				match = 0;
			} else {
				en = (buf[offset + 7] == OPC_ROL) ? MSbF : LSbF;
				ab = buf[offset + 11] - 8;
			}

			offset = find_seq(buf, VISILOAD_CBM_DATA_SIZE, seq_header_bytes, sizeof(seq_header_bytes) / sizeof(seq_header_bytes[0]));
			if (offset == -1) {
				match = 0;
			} else {
				ah = buf[offset + 1] - 3;
			}

			offset = find_seq(buf, VISILOAD_CBM_DATA_SIZE, seq_pulse_threshold, sizeof(seq_pulse_threshold) / sizeof(seq_pulse_threshold[0]));
			if (offset == -1) {
				match = 0;
			} else {
				threshold = buf[offset + 1] + 256 * buf[offset + 6];
			}

			slice_start = blk[ib]->p4 + 1;	/* Set to CBM Dta end + 1 */

		}

		/* Parameter extraction successful? */
		if (match == 0) {
			cbm_index += 2;
			continue;
		}

		sprintf(lin,
			"  Visiload variables found and set: ah=$%02X, ab=$%02X, th=$%04X, en=%s", 
			ah, 
			ab, 
			threshold, 
			ENDIANNESS_TO_STRING(en));
		msgout(lin);

		/* Set the Tx type for further decoding now */
		switch (threshold) {
			case 0x01B6:
				visi_type = VISI_T1;
				break;

			case 0x01E6:
				visi_type = VISI_T2;
				break;

			case 0x01F8:
				visi_type = VISI_T3;
				break;

			case 0x0243:
				visi_type = VISI_T4;
				break;

			case 0x0291:
				visi_type = VISI_T5;
				break;

			case 0x0159:
				visi_type = VISI_T6;
				break;

			case 0x0222:
				visi_type = VISI_T7;
				break;

			default:
				visi_type = VISI_T2;	/* Was the default in Final Tap too */
				break;
		}

		/* 
		 * Search for the next set of CBM files, if any, because we only 
		 * want to look for a Visiload file chain in between two CBM boots
		 */
		do {

			ib = find_decode_block(CBM_HEAD, cbm_index + 2);
			if (ib == -1)
				slice_end = tap.len;
			else
				slice_end = blk[ib]->p1;

			cbm_index += 2;

		} while (slice_end < slice_start);

		/*
		 * Optimize search: If there's a CBM Data (repeated) file 
		 * in between, then start scanning from its end
		 */
		ib  = find_decode_block(CBM_DATA, cbm_index - 1);
		if (ib != -1) {
			if (blk[ib]->p4 < slice_end)
				slice_start = blk[ib]->p4 + 1;
		}

		sprintf(lin,
			" - scanning from $%04X to $%04X\n", 
			slice_start,
			slice_end);
		msgout(lin);

		visiload_search_core(slice_start, slice_end, ah, ab, en);
	}
}
/*---------------------------------------------------------------------------
*/
int visiload_describe(int row)
{
   int i,s,xsb,ab,ah;
   int hd[HDSZ+1];
   int b,rd_err;

   xsb =(blk[row]->xi & 128)>>7;    /* get endianness. */
   ah = (blk[row]->xi & 56)>>3;     /* get no. additional header bytes. */
   ab = (blk[row]->xi & 7);         /* and no. additional bits per byte. */

   /* decode the header... */
   s = blk[row]->p2;
   for(i=ah; i<HDSZ+ah+1; i++)
      hd[i-ah] = visiload_readbyte(s+(i*(8+ab)), xsb, ab, 0);

   /* compute C64 start address, end address and size... */
   blk[row]->cs = (hd[2]<<8)+ hd[3];
   blk[row]->ce = ((hd[0]<<8)+ hd[1])-1;
   blk[row]->cx = ((blk[row]->ce - blk[row]->cs)+1) & 0xFFFF;

   strcpy(lin,"");
   switch(blk[row]->cs)
   {
      case(0x034B) :
         strcat(lin,"\n - MODIFIER : first data byte holds no. of bits per byte in next block."); break;
      case(0x03A4) :
         strcat(lin,"\n - MODIFIER : first data byte is number of additional header bytes+3 in next."); break;
      case(0x0347) :
         strcat(lin,"\n - MODIFIER : if first data byte is $26 then next block will be MSbF else LSbF."); break;
      case(0x03BB) :
         strcat(lin,"\n - MODIFIER : next block (only) will have PILOT tone before it + possibly a pause."); break;
      default :
         strcat(lin,"\n - Next block will be formatted same as this one.");
   }
   strcat(info,lin);

   /* show the first data byte if its useful... */
   if(blk[row]->cs==0x034B || blk[row]->cs==0x03A4 || blk[row]->cs==0x0347)
   {
      sprintf(lin,"\n - First byte : $%02X",hd[4]);
      strcat(info,lin);
   }
   /* print block format info...*/
   sprintf(lin,"\n - Bits per byte : %d | Endianness : %s | Extra headers bytes : %d",8+ab, ENDIANNESS_TO_STRING(xsb), ah);
   strcat(info,lin);

   /* get pilot & trailer lengths... */
   blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1) / (8+ab);
   if(blk[row]->pilot_len>0)
      blk[row]->pilot_len--;  /* discount sync if there is a pilot.*/
   blk[row]->trail_len= 0;


   /* extract data... */

   /* optionally dont decode "modifier" blocks...
     any block that loads to $03xx is ignored. */
   if(extvisipatch==FALSE && (blk[row]->cs & 0xFF00) == 0x0300)
      return(0);

   rd_err=0;
   s = (blk[row]->p2)+((ah+HDSZ)*(8+ab));

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);
   
   for(i=0; i< blk[row]->cx; i++)
   {
      b = visiload_readbyte(s+(i*(8+ab)), xsb, ab, 0);
      if(b==-1)
         rd_err++;
      blk[row]->dd[i] =b;
   }
   blk[row]->rd_err = rd_err;
   return 0;
}


