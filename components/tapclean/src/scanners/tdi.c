/*---------------------------------------------------------------------------
  tdi.c (Tengen Domark Imageworks)

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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define HDSZ 3 /* At least */

/*---------------------------------------------------------------------------
*/
void tdi_search(void)
{
   int i,j,sof,sod,eod,eof;
   int z,hd[10],hdsz;
   int x;

   if(!quiet)
      msgout("  TDI F1");
         
   
   for(i=20; i<tap.len-8; i++)
   {
      if((z=find_pilot(i,TDI_F1))>0)
      {
         sof=i;
         i=z;
         /* verify sync sequence... 0A,09,08,07,06,05,04,03,02,01 */
         for(j=0; j<10; j++)
         {
            if(readttbyte(i, ft[TDI_F1].lp, ft[TDI_F1].sp, ft[TDI_F1].tp, ft[TDI_F1].en)==ft[TDI_F1].sv-j)
               i+=8;
            else
               break;
         }
         if(j==10)  /* all sync sequence present?... */
         {
            sod=i;
            hdsz=3;     /* this is larger for multiload filetype. */

            /* decode header (note : could be either type)... */
            for(j=0; j<HDSZ; j++)
            {
               hd[j]= readttbyte(sod+(j*8), ft[TDI_F1].lp, ft[TDI_F1].sp, ft[TDI_F1].tp, ft[TDI_F1].en);
               if (hd[j] == -1)
                  break;
            }
            if (j != HDSZ)
               continue;

            x= hd[0]+(hd[1]<<8);   /* decode data length */

            eod=sod+((hdsz+x)*8);
            eof=eod+7;

            /* Trace 'eof' to end of trailer (any value, both bit 1 and bit 0 pulses)
               Note: also check a different implementation that uses readttbit()) */
            j=0;
            while (eof < tap.len - 1 && j++ < 8 &&
                  ((tap.tmem[eof + 1] > ft[TDI_F1].sp - tol && /* no matter if overlapping occurrs here */
                  tap.tmem[eof + 1] < ft[TDI_F1].sp + tol) ||
                  (tap.tmem[eof + 1] > ft[TDI_F1].lp - tol && 
                  tap.tmem[eof + 1] < ft[TDI_F1].lp + tol)))
               eof++;

            /* F2 files always appear to have a 0 as load address low.
               F1 files fileID is always at least 01 (same pos as F2 load address low)
               ie. remove this check to allow adding of (incorrect) F2 files.. */
            if(hd[2]!=0)
               addblockdef(TDI_F1, sof,sod,eod,eof, 0);
            i=eof;  /* optimize search */

         }
      }
      else
      {
         if(z<0)    /* find_pilot() failed (too few/many), set i to failure point. */
            i=(-z);
      }
   }
}
/*---------------------------------------------------------------------------
*/
int tdi_describe(int row)
{
   int i,s,hd[10],a,cb,b,hdsz;

   hdsz=3;

   /* decode header (note : could be either type)... */
   s= blk[row]->p2;
   for(i=0; i<hdsz; i++)
      hd[i] = readttbyte(s+(i*8), ft[TDI_F1].lp, ft[TDI_F1].sp, ft[TDI_F1].tp, ft[TDI_F1].en);

   blk[row]->cs= 0;
   blk[row]->cx= hd[0]+(hd[1]<<8);
   blk[row]->ce= blk[row]->cs+ blk[row]->cx -1;

   sprintf(lin,"\n - Block Number : $%02X",hd[2]);
   strcat(info,lin);

   /* get pilot & trailer lengths */
   blk[row]->pilot_len= (blk[row]->p2- blk[row]->p1 -80)>>3;
   blk[row]->trail_len=0;
                       
   /* extract data and test checksum... */
   s= blk[row]->p2+(hdsz*8);
   cb= 0;
   a= 0;

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd= (unsigned char*)malloc(blk[row]->cx);

   for(i=0; i<blk[row]->cx; i++)
   {
      b= readttbyte(s+(i*8), ft[TDI_F1].lp, ft[TDI_F1].sp, ft[TDI_F1].tp, ft[TDI_F1].en);
      if(b==-1)
         blk[row]->rd_err++;
      b= b^a;  /* decipher. */
      a= (a+1)&0xFF;
      cb^= b;
      blk[row]->dd[i]= b;
   }
   b= readttbyte(s+(i*8), ft[TDI_F1].lp, ft[TDI_F1].sp, ft[TDI_F1].tp, ft[TDI_F1].en); /* read actual checkbyte. */

   blk[row]->cs_exp= cb &0xFF;
   blk[row]->cs_act= b;

   return 0;
}



 
