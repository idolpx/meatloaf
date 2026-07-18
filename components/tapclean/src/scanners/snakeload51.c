/*---------------------------------------------------------------------------
  snakeload51.c (Snakeload 5.1)

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

#define HDSZ 13

/*---------------------------------------------------------------------------
*/
void snakeload51_search(void)
{
   int i,sof,sod,eod,eof;
   int z,h,hd[HDSZ];
   unsigned int s,e,x;

   if(!quiet)
      msgout("  Snakeload 5.1");
         
   
   for(i=20; i<tap.len-8; i++)
   {
      if((z=find_pilot(i,SNAKE51))>0)
      {
         sof=i;
         i=z;
         if(readttbit(i, ft[SNAKE51].lp, ft[SNAKE51].sp, ft[SNAKE51].tp)==ft[SNAKE51].sv)
         {
            i++;   /* skip sync bit */
            sod=i;
            /* decode the header so we can validate the addresses... */
            for(h=0; h<HDSZ; h++)
            {
               hd[h] = readttbyte(sod+(h*8), ft[SNAKE51].lp, ft[SNAKE51].sp, ft[SNAKE51].tp, ft[SNAKE51].en);
               if (hd[h] == -1)
                  break;
            }
            if (h != HDSZ)
               continue;

            /* check for known identifier string... */
            if((hd[0]==0xB2 && hd[1]==0xB4 && hd[2]==0xB2 && hd[3]==0xD4) ||
               (hd[0]==0x85 && hd[1]==0x8C && hd[2]==0x8C && hd[3]==0x85) ||
               (hd[0]==0xC8 && hd[1]==0xC3 && hd[2]==0xD4 && hd[3]==0xC9))
            {

               s=hd[12]+(hd[11]<<8);   /* get load address */
               e=hd[10]+(hd[9]<<8);    /* get start address */

               if(e>s)
               {
                  x=e-s;
                  eod=sod+((x+HDSZ)*8);
                  eof=eod+7;

                  /*trace 'eof' to end of trailer (skip through S pulses)... */
                  if(eof>0 && eof<tap.len-1)
                  {
                     while(eof<tap.len-1 && tap.tmem[eof+1]>ft[SNAKE51].sp-tol && tap.tmem[eof+1]<ft[SNAKE51].sp+tol)
                        eof++;
                  }

                  addblockdef(SNAKE51, sof,sod,eod,eof, 0);
                  i=eof;  /* optimize search */
               }
            }
         }
      }
      else
      {
         if(z<0)    /* find_pilot failed (too few/many), set i to failure point. */
            i=(-z);
      }
   }
}
/*---------------------------------------------------------------------------
*/
int snakeload51_describe(int row)
{
   int i,s,b,hd[HDSZ],rd_err,cb;
   
   /* decode the header to get load address etc... */
   s=blk[row]->p2;
   for(i=0; i<HDSZ; i++)
      hd[i] = readttbyte(s+(i*8), ft[SNAKE51].lp, ft[SNAKE51].sp, ft[SNAKE51].tp, ft[SNAKE51].en);

   blk[row]->cs = hd[12]+(hd[11]<<8);
   blk[row]->ce = hd[10]+(hd[9]<<8)-1;
   blk[row]->cx = (blk[row]->ce - blk[row]->cs)+1;     

   /* get pilot & trailer lengths */
   blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1 -8);
   blk[row]->trail_len = (blk[row]->p4 - blk[row]->p3 -7);

   /* extract data and test checksum... */
   rd_err=0;
   cb=0;
   s= blk[row]->p2+(HDSZ*8);

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

   for(i=0; i<blk[row]->cx; i++)
   {
      b= readttbyte(s+(i*8), ft[SNAKE51].lp, ft[SNAKE51].sp, ft[SNAKE51].tp, ft[SNAKE51].en);
      cb^=b;

      if(((blk[row]->cs+i)& 0xFF) ==0xFF)   /* add 1 each time load address is xxFF */
         cb++;

      if(b==-1)
         rd_err++;
      blk[row]->dd[i]=b;
   }
   b= readttbyte(s+(i*8), ft[SNAKE51].lp, ft[SNAKE51].sp, ft[SNAKE51].tp, ft[SNAKE51].en); /* read actual checkbyte. */

   blk[row]->cs_exp= cb &0xFF;
   blk[row]->cs_act= b;
   blk[row]->rd_err= rd_err;

   return 0;
}





