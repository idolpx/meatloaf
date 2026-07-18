/*---------------------------------------------------------------------------
  turbotape_fast.c (Meatloaf addition, GPL v2 or later - same as TAPClean)

  Turbo Tape 64 variant with ~4x faster pulses: bit 0 = ~88 cycles (TAP
  value $0B), bit 1 = ~144 cycles ($12), threshold 116 cycles ($0E). The
  on-tape layout is identical to Turbotape 250: pilot byte $02, countdown
  sync 9..1, header block (type, start, end, byte, 16-char name), then a
  second pilot/sync, a $00 byte, and the data followed by a XOR checkbyte.

  Ported from the wav2prg 'turbotape_fast' loader previously used by
  Meatloaf; scanner structure based on turbotape.c:

  (C) 2001-2006 Stewart Wilson, Subchrist Software.

  Differences from turbotape.c:
   - format table entries TTFAST_HEAD / TTFAST_DATA (fast pulse widths)
   - header type byte $61 accepted (seen on fast-TT64 conversions) in
     addition to the standard 1 (BASIC) and 2 (binary)
---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define LEAD 0x02
#define SYNC 0x09
#define HDSZ 21

#define DATA_FILENAME_SUFFIX "_DATA"

/*---------------------------------------------------------------------------
*/
void turbotape_fast_search(void)
{
   int i,cnt2,sof,sod,eod,eof,zcnt,z,hsize,psize;
   unsigned char byt,pat[32];
   int hd[HDSZ];

   if(!quiet)
      msgout("  Turbotape 64 Fast");


   for(i=20; i<tap.len-8; i++)
   {
      if((z=find_pilot(i,TTFAST_HEAD))>0)
      {
         sof=i;
         i=z;
         if(readttbyte(i, ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF)==SYNC)  /* ending with a sync. */
         {
            for(cnt2=0; cnt2<9; cnt2++)
               pat[cnt2] = readttbyte(i+(cnt2*8), ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF);   /* decode a 10 byte sequence */

            if(pat[0]==9 && pat[1]==8 && pat[2]==7 && pat[3]==6 && pat[4]==5 &&
            pat[5]==4 && pat[6]==3 && pat[7]==2 && pat[8]==1)
            {
               sod = i+72;  /* sod points to start of ID byte */

               /* decode the first byte to see if its a header or a data block... */
               byt = readttbyte(sod, ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF);

               if(byt==1 || byt==2 || byt==0x61)  /* it's a header... */
               {
                  /* decode first few so we can get the program size (needed later)... */
                  for(cnt2=0; cnt2<8; cnt2++)
                  {
                     hd[cnt2] = readttbyte(sod+(cnt2*8), ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF);
                     if (hd[cnt2] == -1)
                        break;
                  }
                  if (cnt2 != 8)
                     continue;

                  psize = (hd[3]+(hd[4]<<8)) - (hd[1]+(hd[2]<<8)) +1; /*  end addr - start addr. */

                  /* now scan through any 0x20's after the used header bytes... (to find its end) */
                  zcnt=22;
                  if(readttbyte(sod+(zcnt*8), ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF)==0x20)
                  {
                     do
                     {
                        byt = readttbyte(sod+(zcnt*8), ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF);
                        zcnt++;
                     }
                     while(byt==0x20);

                     zcnt--;
                     eod = sod+(zcnt*8)-8;
                     eof = eod+7;  /* prepare for poss override. */

                     hsize = (int) ((float)((eod+1) - sod)/8);
                     addblockdef(TTFAST_HEAD, sof,sod,eod,eof, hsize);
                     i = sod+(zcnt*8);  /* optimize search */

                     /* now look for the corresponding program block... */
                     zcnt=0;
                     do        /* find a new lead... */
                     {
                        byt = readttbyte(i, ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF);
                        i++;
                        zcnt++;  /* this records the distance travelled to the prog, must be limited */
                     }
                     while(byt!=LEAD && i<tap.len-8);
                     i--;

                     if(byt==LEAD && zcnt<100)    /* less than 100 pulses travelled to this? */
                     {
                        sof = i;
                        zcnt=0;
                        do    /* trace to its end.. */
                        {
                           byt = readttbyte(i, ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF);
                           i+=8;
                           zcnt++;  /* count leader length (bytes) */
                        }
                        while(byt==LEAD && i<tap.len);

                        if(zcnt>50 && byt==SYNC)  /* if it ends with a sync... */
                        {
                           byt = readttbyte(i+64, ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF);
                           if(byt==0)  /* we can now assume its a prog block. */
                           {
                              sod = i+64;
                              eod = sod+(psize*8); /* see psize calculation above */
                              eof=eod+7;

                              /* now trace to end of trailer (if exists)... */
                              while (eof < tap.len - 1 &&
                                    tap.tmem[eof + 1] > ft[TTFAST_HEAD].sp - tol &&
                                    tap.tmem[eof + 1] < ft[TTFAST_HEAD].sp + tol)
                                 eof++;

                              addblockdef(TTFAST_DATA, sof,sod,eod,eof,0);
                           }
                        }
                     }
                  }
               }
            }
         }
      }
   }
}
/*---------------------------------------------------------------------------
*/
int turbotape_fast_describe(int row)
{
   int i,s;
   int type,b,rd_err,cb;
   int hd[HDSZ+1];  /* +1 coz I save the ID byte here too */
   char ftype[32];
   char fn[256];

   static char _str[256]; /* ASCII filename */
   static /*long*/ int _db_start=0,_db_end=0; /* DATA BLOCK start/end addresses
                                         these are set by header block describe
                                         for use by a following data block describe. */

   /* decode the first few bytes to determine block type... */
   s= blk[row]->p2;
   for(i=0; i<HDSZ+1; i++)
      hd[i]= readttbyte(s+(i*8), ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF);

   /* display filetype */
   type= hd[0];

   if(type==0)
      strcpy(ftype,"DATA");
   if(type==1)
      strcpy(ftype,"HEADER (DATA is BASIC)");
   if(type==2 || type==0x61)
      strcpy(ftype,"HEADER (DATA is Binary)");
   sprintf(lin,"\n - Block type : %s (ID=$%02X)",ftype, type);
   strcat(info,lin);

   /*------------------------------------------------------------------------------*/
   if(type==1 || type==2 || type==0x61)  /* its a HEADER file... */
   {
      /* compute C64 start address, end address and size... */
      blk[row]->cs= 0x033C;
      blk[row]->cx= (blk[row]->p3 - blk[row]->p2)>>3;
      blk[row]->ce= (blk[row]->cs + blk[row]->cx) -1;

      _db_start = hd[1]+ (hd[2]<<8);  /* static save start address of DATA block */
      _db_end = hd[3]+ (hd[4]<<8);    /* static save end address of DATA block */

      sprintf(lin,"\n - DATA FILE Load address : $%04X", _db_start);
      strcat(info,lin);
      sprintf(lin,"\n - DATA FILE End address : $%04X", _db_end);
      strcat(info,lin);

      /* extract file name... */
      for(i=0; i<16; i++)
         fn[i]= hd[6+i];
      fn[i]=0;
      trim_string(fn);
      pet2text(_str,fn);

      sprintf(lin,"\n - Header Size : %d bytes", blk[row]->xi);
      strcat(info,lin);
   }
   /*------------------------------------------------------------------------------*/
   if(type==0)   /* its a DATA file... */
   {
      /* compute C64 start address, end address and size... */
      blk[row]->cs= _db_start;    /* (recalled from previous header) */
      blk[row]->ce= _db_end;
      blk[row]->cx= _db_end - _db_start;
   }

   /* common code for both headers and data files... */

   /* set filename */
   if(blk[row]->fn!=NULL)
      free(blk[row]->fn);
   blk[row]->fn = (char*)malloc(strlen(_str)+((type==0)?strlen(DATA_FILENAME_SUFFIX):0)+1);
   strcpy(blk[row]->fn, _str);
   if(type==0)
      strcat(blk[row]->fn, DATA_FILENAME_SUFFIX);

   /* get pilot trailer lengths... */
   blk[row]->pilot_len= blk[row]->p2- blk[row]->p1 -80;
   blk[row]->trail_len= blk[row]->p4- blk[row]->p3 -7;

   /* extract data and test checksum... */
   rd_err=0;
   cb=0;
   s= (blk[row]->p2)+8; /* +8 skips ID byte */

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

   for(i=0; i<blk[row]->cx; i++)
   {
      b= readttbyte(s+(i*8), ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF);
      cb^=b;
      if(b==-1)
         rd_err++;
      blk[row]->dd[i]=b;
   }
   b= readttbyte(s+(i*8), ft[TTFAST_HEAD].lp, ft[TTFAST_HEAD].sp, ft[TTFAST_HEAD].tp, MSbF); /* read actual cb. */
   blk[row]->cs_exp= cb &0xFF;
   blk[row]->cs_act= b;
   blk[row]->rd_err= rd_err;

   if(blk[row]->lt==TTFAST_HEAD)
      blk[row]->cs_exp = -2;   /* headers dont use checksums. */

   return 0;
}
