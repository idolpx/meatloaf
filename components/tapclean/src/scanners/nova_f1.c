/*---------------------------------------------------------------------------
  nova_f1.c (Novaload)
  
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

  Standard Novaload sub-block chains are dealt with as if they were a single file.

  HERO and DECATHLON  - disable "Virtual device traps" in VICE to get em working.

---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

/*---------------------------------------------------------------------------
*/
void nova_search(void)
{
   int i,j,s,e,x;
   int sof,sod,eod,eof,z;
   int len,total_blocks;
   int b1,b2,hd[256];
   double ftmp;

   if(!quiet)
      msgout("  Novaload");

   for(i=20; i>0 && i<tap.len-8; i++)
   {
      if((z=find_pilot(i,NOVA))>0)
      {
         sof=i;
         i=z;    /* zcnt>1800 && zcnt<3000 */
         if(readttbit(i,ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp)==ft[NOVA].sv) /* got sync bit? */
         {
            i++;  /* jump over sync bit. */
            b1= readttbyte(i, ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, ft[NOVA].en);
            b2= readttbyte(i+8, ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, ft[NOVA].en);

            /* normal Novaload block (or Ocean type 2/3 with short (>1800,<3000) leader!) */
            if(b1==0xAA && b2!=0x55)  /* could be Novaload or ocean types 2/3... */
            {
               sod=i+8;     /* save data start pos at byte after the $AA */

               /* decode header... */
               for(j=0; j<32; j++)
               {
                  hd[j]= readttbyte(sod+(j*8), ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, ft[NOVA].en);
                  if(hd[j]==-1)
                  {
                     if(!quiet)
                     {
                        sprintf(lin,"\n * Read error in NOVALOAD header ($%04X), search abandoned.",sod+(j*8));
                        msgout(lin);
                        return;
                     }
                  }
               }

               len= (int)hd[0];  /* get length of filename... */
               /* get total length */
               x= (hd[len+5]+(hd[len+6]<<8)-256) & 0xFFFF;  /* length written is +256. */

               /* now compute it from start and end addresses (as a precaution) */
               s= hd[len+1] + (hd[len+2]<<8) +256;  /* addr written is -256. */
               e= hd[len+3] + (hd[len+4]<<8);

               if(x!=(e-s))  /* this calculation IS correct. */
                  x=(e-s);

               ftmp= ceil((double)x/256);  /* compute total no. blocks  (inc. any <256 bytes!) */
               total_blocks= (int)ftmp;    /* this count is needed to compute the EOF */

               if(total_blocks>0)   /* jetsons side 2 revealed a negative value here causing a lockup (fixed by this). */
               {
                  eod= sod + (x*8) + (total_blocks*8) +((7+len)*8);
                  eof= eod+7;

                  /* trace 'eof' to end of trailer... */
                  if(eof>0 && eof<tap.len-1)
                  {
                     while(eof<tap.len-1 && tap.tmem[eof+1]>ft[NOVA].sp-tol && tap.tmem[eof+1]<ft[NOVA].sp+tol)
                        eof++;
                  }
                  addblockdef(NOVA, sof,sod,eod,eof, 0);
                  i=eof;
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
int nova_describe(int row)
{
   int s,i,tmp,blen,cnt;
   int b,rd_err,hd[256], cb;  /* novaload headers can be ANY size! */
   int fnlen,subs,full;
   double ftmp;
   char fn[256];
   char str[2000];

   /* decode the header... (32 bytes, i should alter this to the real header size) */
   s= blk[row]->p2;
   for(i=0; i<32; i++)
      hd[i]= readttbyte(s+(i*8), ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, ft[NOVA].en);

   /* extract file name... */
   fnlen=hd[0];
   for(i=0; i<fnlen; i++)
      fn[i]= hd[1+i];
   fn[i]=0;
   trim_string(fn);
   pet2text(str,fn);

   if(blk[row]->fn!=NULL)
      free(blk[row]->fn);
   blk[row]->fn = (char*)malloc(strlen(str)+1);
   strcpy(blk[row]->fn, str);
   
   /* compute C64 start address, end address and size... */
   blk[row]->cs= hd[fnlen+1]+ (hd[fnlen+2]<<8) +256;
   blk[row]->ce= hd[fnlen+3]+ (hd[fnlen+4]<<8)-1;
   blk[row]->cx= (hd[fnlen+5]+ (hd[fnlen+6]<<8) -256)&0xFFFF;

   if(blk[row]->cx != (blk[row]->ce - blk[row]->cs)+1)  /* dont trust data-length field. */
   {
      blk[row]->cx=  blk[row]->ce - blk[row]->cs;
      sprintf(lin,"\n * Mistrusted written Novaload size, using computed.");
      strcat(info,lin);
   }

   ftmp= ceil((double)blk[row]->cx/256);  /* compute total no. blocks  (inc. any <256 bytes) */
   subs= (int)ftmp;
   ftmp= floor((double)blk[row]->cx/256); /* compute total no. blocks (!inc. any <256 bytes) */
   full= (int)ftmp;
   sprintf(lin,"\n - Sub-blocks : %d (%d full)",subs,full);
   strcat(info,lin);

   /* decode all blocks... */
   s= (blk[row]->p2)+((fnlen+8)*8);

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);
   
   cnt=0;
   rd_err=0;
   do
   {
      for(i=0; i<256 && cnt<blk[row]->cx; i++)
      {
         b= readttbyte(s+(i*8), ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, ft[NOVA].en);
         if(b==-1)
            rd_err++;
         blk[row]->dd[cnt++]= b;
      }
      s+=(257*8); /* nudge to next block start. (skips 'checksum so far') */
   }
   while(cnt < blk[row]->cx);

   blk[row]->rd_err= rd_err;

   /* get pilot & trailer lengths */
   blk[row]->pilot_len= blk[row]->p2 - blk[row]->p1;
   blk[row]->trail_len= blk[row]->p4 - blk[row]->p3 -7;
           
   /* Verify checksum.... (this could be merged into the decoder above) */
   s= blk[row]->p2;
   for(i=0; i<32; i++)
      hd[i]= readttbyte(s+(i*8), ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, ft[NOVA].en);

   tmp= (int)hd[0];               /* get length of filename... */
   blen= blk[row]->cx;            /* get total length... */
   ftmp= ceil((double)blen/256);   /* compute total no. blocks  (inc. any <256 bytes!) */
   blen+= ((int)ftmp+tmp+7);           /* add in the extra byte (checksum) for each sub block  + main header. */
   cb= 0;

   for(i=0; i<blen; i++)
   {
      b= readttbyte(s+(i*8), ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, ft[NOVA].en);
      cb= (cb+b) &0xFF;
   }
   b= readttbyte(s+(i*8), ft[NOVA].lp, ft[NOVA].sp, ft[NOVA].tp, ft[NOVA].en);  /* read the FINAL checkbyte */

   blk[row]->cs_exp= cb &0xFF;
   blk[row]->cs_act= b;

   return 0;
}



