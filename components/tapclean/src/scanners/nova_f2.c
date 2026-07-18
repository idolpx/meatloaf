/*---------------------------------------------------------------------------
  nova_f2.c (Novaload Special)
  
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

/*---------------------------------------------------------------------------
*/
void nova_spc_search(void)
{
   int i,j,sof,sod,eod,eof, z;
   int ipulse,start,zcnt;
   int b,b2, done;

   if(!quiet)
      msgout("  Novaload Special");
         

   for(i=20; i>0 && i<tap.len-200; i++)
   {
      if((z=find_pilot(i,NOVA))>0)
      {
         zcnt= z-i+1;    /* store pilot length. */
         i=z;
         if(readttbit(i,ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp)==ft[NOVA].sv) /* got sync bit? */
         {
            i++;  /* jump over sync bit. */
            b= readttbyte(i, ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, LSbF);    /* look for a lead-in byte, LSbF first */
            b2= readttbyte(i+8, ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, LSbF);

            if(b==0xAA && b2==0x55)  /* DEFINITELY a NL special block chain... */
            {
               i+=16;
               ipulse= i-zcnt-16;  /* mark the start pulse of pilot */
               start= i;          /* and first sub-blocks address byte */

               done=0;
               do
               {
                  b= readttbyte(i, ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, LSbF); /* read address (high) byte */
                  if(b==0)
                  {
                     /* now i rewind and add each sub block individually... */
                     j=0;
                     do
                     {
                        b= readttbyte(start+(j*2064), ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, LSbF);
                        if(b!=0x0)
                        {
                           if(j==0)
                              sof= ipulse;  /* use pilot start for first block */
                           else
                              sof= start+(j*2064);
                           sod= start+(j*2064);
                           eod= sod+2056;
                           eof= eod+7;

                           /* now we check to see if this is the last block, if so
                             we can trace the trailer to its end and put eof
                             there instead... */
                           if(readttbyte(eof+1, ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, LSbF)==0x00)
                           {
                              while(eof<tap.len-1 && tap.tmem[eof+1]>ft[NOVA].sp-tol && tap.tmem[eof+1]<ft[NOVA].sp+tol)
                                 eof++;
                           }
                           addblockdef(NOVA_SPC,sof,sod,eod,eof,0);
                        }
                        j++; /* bump to next sub-block. */
                     }
                     while(b!=0x0);
                     done=1;
                  }

                  if(b!=0)
                     i+=2064; /* jump to next sub-block... */
               }
               while(!done && i<tap.len);
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
int nova_spc_describe(int row)
{
   int i,s,b,hd[5],rd_err,cb;

   s= blk[row]->p2;

   for(i=0;i<5; i++)
      hd[i]= readttbyte(s+(i*8), ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, LSbF);

   /* compute C64 start address, end address and size... */
   blk[row]->cs= hd[0]<<8;
   blk[row]->ce= blk[row]->cs + 255;
   blk[row]->cx= 256;

   /* get pilot trailer lengths... */
   blk[row]->pilot_len = blk[row]->p2- blk[row]->p1;
   if(blk[row]->pilot_len!=0)
      blk[row]->pilot_len-=17;  /* adjust computation for initial block. */
   blk[row]->trail_len = blk[row]->p4- (blk[row]->p3+7);
     
   /* extract data and test checksum... */
   rd_err=0;
   /* note: start address is inluded in checksum so its initialized here first.. */
   s= blk[row]->p2;
   cb= readttbyte(s, ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, LSbF);

   s= blk[row]->p2 +8; /* +8 skips load address byte */

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);
   
   for(i=0; i<blk[row]->cx; i++)
   {
      b= readttbyte(s+(i*8), ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, LSbF);
      cb=(cb+b)&0xFF;
      if(b==-1)
         rd_err++;
      blk[row]->dd[i]= b;
   }
   b= readttbyte(s+(i*8), ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, LSbF); /* read actual checksum */ 

   blk[row]->cs_exp= cb;
   blk[row]->cs_act= b;
   blk[row]->rd_err= rd_err;
   return 0;
}








 
 
