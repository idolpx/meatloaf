/*---------------------------------------------------------------------------
  turbotape.c

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

  9/12/2001 - added trailer tracing, only 1 tap i found so far actually has trailers.
            - pilot/trailer length calc is 100%.


this one is dealt with a bit specially, it has separate headers for each program block,
the header size is variable, so needs special location technique, the following program
block has to be located by looking at the size in the header.

the upshot is that the blocks come in pairs, and they must be located in pairs also...

first a header is located properly, then a program block is searched for, it MUST be
within 100 pulses from the tail end of the header for the pair to qualify.


further notes:-

 - only BASIC type program blocks have an ID byte in front of them

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
void turbotape_search(void)
{
   int i,cnt2,sof,sod,eod,eof,zcnt,z,hsize,psize;
   unsigned char byt,pat[32];
   int hd[HDSZ];

   if(!quiet)
      msgout("  Turbotape 250 (+clones)");


   for(i=20; i<tap.len-8; i++)
   {
      if((z=find_pilot(i,TT_HEAD))>0)
      {
         sof=i;
         i=z;
         if(readttbyte(i, ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF)==SYNC)  /* ending with a sync. */
         {
            for(cnt2=0; cnt2<9; cnt2++)
               pat[cnt2] = readttbyte(i+(cnt2*8), ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF);   /* decode a 10 byte sequence */

            if(pat[0]==9 && pat[1]==8 && pat[2]==7 && pat[3]==6 && pat[4]==5 &&
            pat[5]==4 && pat[6]==3 && pat[7]==2 && pat[8]==1)
            {
               sod = i+72;  /* sod points to start of ID byte */

               /* decode the first byte to see if its a header or a data block... */
               byt = readttbyte(sod, ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF);

               if(byt==1 || byt==2)  /* it's a header... */
               {
                  /* decode first few so we can get the program size (needed later)... */
                  for(cnt2=0; cnt2<8; cnt2++)
                  {
                     hd[cnt2] = readttbyte(sod+(cnt2*8), ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF);
                     if (hd[cnt2] == -1)
                        break;
                  }
                  if (cnt2 != 8)
                     continue;

                  psize = (hd[3]+(hd[4]<<8)) - (hd[1]+(hd[2]<<8)) +1; /*  end addr - start addr. */

                  /* now scan through any 0x20's after the used header bytes... (to find its end) */
                  zcnt=22;
                  if(readttbyte(sod+(zcnt*8), ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF)==0x20)
                  {
                     do
                     {
                        byt = readttbyte(sod+(zcnt*8), ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF);
                        zcnt++;
                     }
                     while(byt==0x20);

                     /* Support for "Micrus Copy" (used in Polish game compilations) */
                     if ((byt == 0x78 && zcnt == 54) || (byt == 0xEA && zcnt == 27) || (byt == 0x78 && zcnt == 58))
                        zcnt = 193;

                     zcnt--;
                     eod = sod+(zcnt*8)-8;
                     eof = eod+7;  /* prepare for poss override. */

                     hsize = (int) ((float)((eod+1) - sod)/8);
                     addblockdef(TT_HEAD, sof,sod,eod,eof, hsize);
                     i = sod+(zcnt*8);  /* optimize search */

                     /* now look for the corresponding program block... */
                     zcnt=0;
                     do        /* find a new lead... */
                     {
                        byt = readttbyte(i, ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF);
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
                           byt = readttbyte(i, ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF);
                           i+=8;
                           zcnt++;  /* count leader length (bytes) */
                        }
                        while(byt==LEAD && i<tap.len);

                        if(zcnt>50 && byt==SYNC)  /* if it ends with a sync... */
                        {
                           byt = readttbyte(i+64, ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF);
                           if(byt==0)  /* we can now assume its a prog block. */
                           {
                              sod = i+64;
                              eod = sod+(psize*8); /* see psize calculation above */
                              eof=eod+7;

                              /* now trace to end of trailer (if exists)... */
                              while (eof < tap.len - 1 &&
                                    tap.tmem[eof + 1] > ft[TT_HEAD].sp - tol &&
                                    tap.tmem[eof + 1] < ft[TT_HEAD].sp + tol)
                                 eof++;

                              addblockdef(TT_DATA, sof,sod,eod,eof,0);
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
int turbotape_describe(int row)
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
      hd[i]= readttbyte(s+(i*8), ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF);

   /* display filetype */
   type= hd[0];

   if(type==0)
      strcpy(ftype,"DATA");
   if(type==1)
      strcpy(ftype,"HEADER (DATA is BASIC)");
   if(type==2)
      strcpy(ftype,"HEADER (DATA is Binary)");
   sprintf(lin,"\n - Block type : %s (ID=$%02X)",ftype, type);
   strcat(info,lin);

   /*------------------------------------------------------------------------------*/
   if(type==1 || type==2)  /* its a HEADER file... */
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
      b= readttbyte(s+(i*8), ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF);
      cb^=b;
      if(b==-1)
         rd_err++;
      blk[row]->dd[i]=b;
   }
   b= readttbyte(s+(i*8), ft[TT_HEAD].lp, ft[TT_HEAD].sp, ft[TT_HEAD].tp, MSbF); /* read actual cb. */
   blk[row]->cs_exp= cb &0xFF;
   blk[row]->cs_act= b;
   blk[row]->rd_err= rd_err;

   if(blk[row]->lt==TT_HEAD)
      blk[row]->cs_exp = -2;   /* headers dont use checksums. */

   return 0;
}






