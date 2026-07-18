/*---------------------------------------------------------------------------
  rasterload.c

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

---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <stdlib.h>

#define HDSZ 4

/*---------------------------------------------------------------------------
*/
void raster_search(void)
{
   int sof,sod,eod,eof, i;
   int z,tcnt,hd[HDSZ],x;

   if(!quiet)
      msgout("  Rasterload");
         

   for(i=20; i<tap.len-8; i++)
   {
      if((z=find_pilot(i,RASTER))>0)
      {
         sof=i;
         i=z;
         if(readttbyte(i, ft[RASTER].lp, ft[RASTER].sp, ft[RASTER].tp, ft[RASTER].en)==ft[RASTER].sv)
         {
            sod=i+8;
            /* decode the header, so we can validate the addresses */
            for(tcnt=0; tcnt<HDSZ; tcnt++)
            {
               hd[tcnt] = readttbyte(sod+(tcnt*8), ft[RASTER].lp, ft[RASTER].sp, ft[RASTER].tp, ft[RASTER].en);
               if (hd[tcnt] == -1)
                  break;
            }
            if (tcnt != HDSZ)
               continue;

            x = (hd[2]+(hd[3]<<8)) - (hd[0]+(hd[1]<<8))+1;   /* compute length */
            if(x>0)
            {
               eod = sod+ ((x+HDSZ)*8);
               eof=eod+7;

               /* there's usually an $FF byte after checksum, but i cant
                assume it is there or is unbroken.
                grod the pixie has a broken one. */

               while(eof<tap.len-1 && tap.tmem[eof+1]>ft[RASTER].lp-tol && tap.tmem[eof+1]<ft[RASTER].lp+tol)
                  eof++;

               addblockdef(RASTER, sof,sod,eod,eof, 0);
               i = eof;  /* optimize search */
            }
         }
      }
   }
}
/*---------------------------------------------------------------------------
*/
int raster_describe(int row)
{
   int i,s,b,hd[HDSZ],rd_err,cb;

   s= blk[row]->p2;

   for(i=0; i<HDSZ; i++)    /* decode the header... */
   {
      b= readttbyte(s+(i*8), ft[RASTER].lp, ft[RASTER].sp, ft[RASTER].tp, ft[RASTER].en);
      hd[i]=b;
   }

   /* compute C64 start address, end address and size... */
   blk[row]->cs = (hd[1]<<8) + hd[0];
   blk[row]->ce = (hd[3]<<8) + hd[2];
   blk[row]->cx = (blk[row]->ce - blk[row]->cs)+1;

   /* get pilot & trailer lengths */
   blk[row]->pilot_len = blk[row]->p2 - blk[row]->p1;
   blk[row]->trail_len = blk[row]->p4 - blk[row]->p3 -7;

   /* extract data and test checksum... */
   rd_err=0;
   cb=0;
   s= (blk[row]->p2)+(HDSZ*8);

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);
   
   for(i=0; i<blk[row]->cx; i++)
   {
      b= readttbyte(s+(i*8), ft[RASTER].lp, ft[RASTER].sp, ft[RASTER].tp, ft[RASTER].en);
      cb^=b;
      if(b==-1)
         rd_err++;
      blk[row]->dd[i]=b;
   }
   b= readttbyte(s+(i*8), ft[RASTER].lp, ft[RASTER].sp, ft[RASTER].tp, ft[RASTER].en); /* read actual checkbyte */
   blk[row]->cs_exp= cb &0xFF;
   blk[row]->cs_act= b;
   blk[row]->rd_err= rd_err;
   return 0;
}


