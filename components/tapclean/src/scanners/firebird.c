/*---------------------------------------------------------------------------
  firebird.c

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
 
   Notes..

   These routines scan for 2 threshold types. Firebird T1 and Firebird T2.
---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define SYNC2 0x42
#define HDSZ 4

/*---------------------------------------------------------------------------
*/
void firebird_search(void)
{
   int i,sof,sod,eod,eof;
   int z,j,hd[HDSZ];
   int s,e,x, t,sr;
   int lp,sp,tp,ld;
   int seq[10] = {0xA2,XX,0x8E,0x06,0xDD,0xA2,XX,0x8E,0x07,0xDD};
   int *buf,bufsz;
   char lname[2][16] = {"T1","T2"};

   /* try and find correct threshold in loader... */
   ld= FBIRD_T1;  /* set default type. */
   sr= 0;      /* string reference (lname). */
   t= find_decode_block(CBM_HEAD,1);
   if(t!=-1)
   {
      /* make an 'int' copy of the data... */
      bufsz= blk[t]->cx;
      buf= (int*)malloc(bufsz*sizeof(int));
      for(i=0; i<bufsz; i++)
         buf[i]= blk[t]->dd[i];
      
      i= find_seq(buf,bufsz, seq,10);
      if(i!=-1)
      {
         if(blk[t]->dd[i+1]==0x20 && blk[t]->dd[i+6]==0x03)
         {
            ld= FBIRD_T1;
            sr= 0;      /* string reference (lname). */
            msgout("   Firebird threshold found = T1 ($320 cycles)");
         }
         if(blk[t]->dd[i+1]==0x9A && blk[t]->dd[i+6]==0x02)
         {
            ld= FBIRD_T2;
            sr= 1;      /* string reference (lname). */
            msgout("   Firebird threshold found = T2 ($29A cycles) ");
         }
      }
      free(buf);
   }
   sp=ft[ld].sp;
   lp=ft[ld].lp;
   tp=ft[ld].tp;

   /*-------------------------------------------------------------------*/
   if(!quiet)
   {
      sprintf(lin,"  Firebird Loader %s",lname[sr]);
      msgout(lin);
         
   }
   
   for(i=20; i<tap.len-8; i++)
   {
      if((z=find_pilot(i,ld))>0)
      {
         sof=i;
         i=z;
         if(readttbyte(i,lp,sp,tp,ft[ld].en)==ft[ld].sv)
         {
            if(readttbyte(i+8,lp,sp,tp, ft[ld].en)==SYNC2)
            {
               sod= i+16;

               /* decode the header, so we can validate the addresses... */
               for(j=0; j<HDSZ; j++)
               {
                  hd[j] = readttbyte(sod+(j*8),lp,sp,tp, ft[ld].en);
                  if (hd[j] == -1)
                     break;
               }
               if (j != HDSZ)
                  continue;

               s= hd[0]+ (hd[1]<<8);   /* get start address */
               e= hd[2]+ (hd[3]<<8);   /* get end address */

               if(e>s)
               {
                  x = e-s;
                  eod = sod+ ((x+HDSZ)*8);
                  eof = eod+7;
                  addblockdef(ld, sof,sod,eod,eof, 0);
                  i = eof;  /* optimize search */
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
int firebird_describe(int row)
{
   int i,s,b,hd[HDSZ],rd_err,cb;
   int sp,lp,tp;

   if(blk[row]->lt==FBIRD_T1)
      {sp=ft[FBIRD_T1].sp; lp=ft[FBIRD_T1].lp; tp=ft[FBIRD_T1].tp;}  /* Firebird T1 */
   if(blk[row]->lt==FBIRD_T2)
      {sp=ft[FBIRD_T2].sp; lp=ft[FBIRD_T2].lp; tp=ft[FBIRD_T2].tp;}  /* Firebird T2 */

   /* decode the header... */
   s= blk[row]->p2;
   for(i=0; i<HDSZ; i++)
      hd[i]= readttbyte(s+(i*8), lp,sp,tp, ft[FBIRD_T1].en);

   blk[row]->cs = hd[0]+ (hd[1]<<8);
   blk[row]->ce = hd[2]+ (hd[3]<<8)-1;
   blk[row]->cx = (blk[row]->ce - blk[row]->cs)+1;

   /* get pilot trailer lengths */
   blk[row]->pilot_len = (blk[row]->p2 - blk[row]->p1 -8)>>3;
   blk[row]->pilot_len = (blk[row]->p4 - blk[row]->p3 -7)>>3;

   /* extract data and test checksum... */
   rd_err=0;
   cb=0;
   s= (blk[row]->p2)+(HDSZ*8);

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);
   
   for(i=0; i<blk[row]->cx; i++)
   {
      b= readttbyte(s+(i*8), lp,sp,tp, ft[FBIRD_T1].en);
      cb^=b;
      if(b==-1)
         rd_err++;
      blk[row]->dd[i]=b;
   }
   b= readttbyte(s+(i*8), lp,sp,tp, ft[FBIRD_T1].en);  /* read actual cb. */
   blk[row]->cs_exp= cb &0xFF;
   blk[row]->cs_act= b;
   blk[row]->rd_err= rd_err;
   return 0;
}

