/*---------------------------------------------------------------------------
  seuck.c (Shoot 'em Up Construction Kit)

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


  Note: Supports both the files of the SEUCK itself and also the games produced
  by it.

---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <stdlib.h>

#define GAME_SYNC 0xAC
#define GAME_SIZE 63226

#define L2_DATASIZE 196

/*---------------------------------------------------------------------------
*/
void seuck1_search(void)
{
   int i,sof,sod,eod,eof,z,byt;
   
   if(!quiet)
      msgout("  SEUCK tape");
         
   
   for(i=20; i<tap.len-8; i++)
   {
      if((z=find_pilot(i,SEUCK_L2))>0)
      {
         sof=i;
         i=z;
         /* check for files of type found in the kit itself... */
         if(readttbyte(i, ft[SEUCK_L2].lp, ft[SEUCK_L2].sp, ft[SEUCK_L2].tp, ft[SEUCK_L2].en)==ft[SEUCK_L2].sv)
         {
            byt=readttbyte(i+8, ft[SEUCK_L2].lp, ft[SEUCK_L2].sp, ft[SEUCK_L2].tp, ft[SEUCK_L2].en);

            if(byt==0xA2)   /* its the loader2 file... */
            {
               sod = i+8;
               eod = sod+ (L2_DATASIZE*8);
               eof = eod+7;
               addblockdef(SEUCK_L2, sof,sod,eod,eof, 0);
               i = eof;  /* optimize search */
            }
            if(byt==0xBB)   /* its the header file... */
            {
               sod = i+8;
               eod = sod+ (2*8);
               eof = eod+7;
               addblockdef(SEUCK_HEAD, sof,sod,eod,eof, 0);
               i = eof;  /* optimize search */
            }
            if(byt==0xCC)   /* its a data file... */
            {
               byt=readttbyte(i+16, ft[SEUCK_L2].lp, ft[SEUCK_L2].sp, ft[SEUCK_L2].tp, ft[SEUCK_L2].en);
               sod = i+8;
               eod = sod+ ((byt+2)*8);
               eof = eod+7;
               addblockdef(SEUCK_DATA, sof,sod,eod,eof, 0);
               i = eof;  /* optimize search */
            }
            if(byt==0xAA)   /* its the trigger file... */
            {
               sod = i+8;
               eod = sod+ (2*8);
               eof = eod+7;
               addblockdef(SEUCK_TRIG, sof,sod,eod,eof, 0);
               i = eof;  /* optimize search */
            }
         }

         /* maybe we have found a SEUCK game... */
         else if(readttbyte(i, ft[SEUCK_L2].lp, ft[SEUCK_L2].sp, ft[SEUCK_L2].tp, ft[SEUCK_L2].en)==GAME_SYNC)
         {
            sod = i+8;
            eod = sod+ (GAME_SIZE*8);
            eof = eod+7;
            addblockdef(SEUCK_GAME, sof,sod,eod,eof, 0);
            i = eof;  /* optimize search */
         }
      }
   }
}
/*---------------------------------------------------------------------------
*/
int seuck1_describe(int row)
{
   int i,s,b,rd_err,cb,skip,b1,b2;
   static int base=0;

   if(blk[row]->lt==SEUCK_L2)
   {
      blk[row]->cs = 0x000A;
      blk[row]->ce = 0x00CD;
      blk[row]->cx = L2_DATASIZE;
      skip=0;
   }
   if(blk[row]->lt== SEUCK_DATA)
   {
      b=readttbyte(blk[row]->p2+8, ft[SEUCK_L2].lp, ft[SEUCK_L2].sp, ft[SEUCK_L2].tp, ft[SEUCK_L2].en);
      blk[row]->cs = base;  /* set to current base address. */
      blk[row]->cx = b;
      blk[row]->ce = blk[row]->cs + blk[row]->cx -1;
      skip=2;    /* decoder must skip the ID byte and the Size byte. */
      base+=blk[row]->cx; /* bump base. */
   }
   if(blk[row]->lt==SEUCK_HEAD)
   {
      blk[row]->cs = 0x0002;  /* load addr bytes are stored in $0002,$0003. */
      blk[row]->cx = 2;
      blk[row]->ce = blk[row]->cs + blk[row]->cx -1;
      skip=1;    /* decoder must skip the ID byte. */

      /* set base address... */
      b1=readttbyte(blk[row]->p2+8, ft[SEUCK_L2].lp, ft[SEUCK_L2].sp, ft[SEUCK_L2].tp, ft[SEUCK_L2].en);
      b2=readttbyte(blk[row]->p2+16, ft[SEUCK_L2].lp, ft[SEUCK_L2].sp, ft[SEUCK_L2].tp, ft[SEUCK_L2].en);
      base = b1+(b2<<8);
   }
   if(blk[row]->lt==SEUCK_TRIG)
   {
      blk[row]->cs = 0x0000;
      blk[row]->cx = 2;
      blk[row]->ce = blk[row]->cs + blk[row]->cx -1;
      skip=1;    /* decoder must skip the ID byte. */
   }
   if(blk[row]->lt==SEUCK_GAME)
   {
      blk[row]->cs = 0x0900;
      blk[row]->cx = GAME_SIZE;
      blk[row]->ce = blk[row]->cs + blk[row]->cx -1;
      skip=0;
   }

   /* get pilot & trailer lengths */
   blk[row]->pilot_len= (blk[row]->p2- blk[row]->p1 -8)>>3;
   blk[row]->trail_len= (blk[row]->p4- blk[row]->p3 -7)>>3;

   /* extract data and test checksum... */
   rd_err=0;
   cb=0;
   s= blk[row]->p2+(skip*8);

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd= (unsigned char*)malloc(blk[row]->cx);

   for(i=0; i<blk[row]->cx; i++)
   {
      b= readttbyte(s+(i*8), ft[SEUCK_L2].lp, ft[SEUCK_L2].sp, ft[SEUCK_L2].tp, ft[SEUCK_L2].en);
      cb^=b;
      if(b==-1)
         rd_err++;
      blk[row]->dd[i]=b;
   }
   blk[row]->rd_err = rd_err;

   if(blk[row]->lt==SEUCK_L2 || blk[row]->lt==SEUCK_DATA) /* headers dont use checkbytes. */
   {
      b= readttbyte(s+(i*8), ft[SEUCK_L2].lp, ft[SEUCK_L2].sp, ft[SEUCK_L2].tp, ft[SEUCK_L2].en); /* read actual checkbyte. */
      blk[row]->cs_exp= cb &0xFF;
      blk[row]->cs_act= b;
   }
   return 0;
}



 
