/*---------------------------------------------------------------------------
  ocean.c
  
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

  Ocean type 1 & 2 trail out with bit 0's (S)
  Ocean type 3 trails out with byte SLSSSSSS. ($02)


  Pilot lengths...
     -Rambo 12303
     -Arkanoid 4112

---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <stdlib.h>

/*---------------------------------------------------------------------------
*/
void ocean_search(void)
{
   int k,i,sof,sod,eod,eof;
   int start,z,ipulse, done, byt;

   if(!quiet)
      msgout("  Ocean/Imagine (F1,F2,F3)");
   

   for(i=20; i>0 && i<tap.len-8; i++)
   {
      if((z=find_pilot(i,OCEAN_F1))>0)
      {
         ipulse=i;  /* save 1st pulse of pilot */

         /* bugfix: nudge ipulse forward to first 'non-accounted for' pulse..
          this prevents the problem with cbm/ocean being unseparated by pause
          see Highlander,Comic Bakery,Super Bowl XX,Transformers (UC64TP)
          (last pulse of CBMDATA is mistaken for Ocean pilot).
          I only ever found a +1 needed but this will handle anything. */

         while(is_accounted(ipulse))
            ipulse++;


         i=z; /* i points at sync */

         if(readttbit(i,ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp)==ft[OCEAN_F1].sv) /* got sync bit? */
         {
            i++;  /* jump over sync bit. */
            
            byt= readttbyte(i, ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en);  /* look for sync byte */

            if(byt==0xAA)  /* Ocean types 2 or 3 (or a novaload type with long pilot) */
            {
               i+=16;            /* skip over AA and RAM page byte  */
               start=i;          /* store offset of load address for  */
                                 /* sub-block 1. */

               /* now scan through sub-block load addresses til we find...
                 $02=Ocean 3 or $00=Ocean 2... */
               do
               {
                  done=0;
                  byt= readttbyte(i, ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en);
                  if(byt==0x02)   /* 2 blocks with 0x02 address = OCEAN TYPE 3! */
                  {
                     if(readttbyte(i+8, ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en)==0x02)
                     {
                        /* ok, definitely Ocean type 3...
                         now i rewind and add each sub block individually... */
                        k=0;
                        do
                        {
                           byt=readttbyte(start+(k*2064), ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en);
                           if(byt!=0x02)
                           {
                              if(k==0)
                                 sof=ipulse;  /* use pilot start for first block */
                              else
                                 sof = start+(k*2064);
                              sod = start+(k*2064);
                              eod = sod+2056;
                              eof = eod+7;

                              /* now we check to see if this is the last block, if so
                               we can trace the trailer to its end and put eof
                               there instead... */
                              if(readttbyte(eof+1, ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en)==0x02)
                              {
                                 while(readttbyte(eof+1, ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en)==0x02 && eof<tap.len-1)
                                    eof+=8;
                              }
                              addblockdef(OCEAN_F3,sof,sod,eod,eof,0);
                           }
                           k++; /* bump to next sub-block. */
                        }
                        while(byt!=0x02);
                        done=1;
                     }
                  } /*---------------------------------------------------*/

                  else if(byt==0x00) /* end block with 0x00 start address = OCEAN 2 */
                  {
                     if(readttbyte(i+8, ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en)==0x00)
                     {
                        /* ok, definitely Ocean type 2...
                         now i rewind and add each sub block individually... */
                        k=0;
                        do
                        {
                           byt=readttbyte(start+(k*2064), ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en);
                           if(byt!=0x00)
                           {
                              if(k==0)
                                 sof = ipulse;  /* use pilot start for first block */
                              else
                                 sof = start+(k*2064);
                              sod = start+(k*2064);
                              eod = sod+2056;
                              eof = eod+7;

                              /* now we check to see if this is the last block, if so
                               we can trace the trailer to its end and put eof
                               there instead... */
                              if(readttbyte(eof+1, ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en)==0)
                              {
                                 while(eof<tap.len-1 && tap.tmem[eof+1]>ft[OCEAN_F1].sp-tol && tap.tmem[eof+1]<ft[OCEAN_F1].sp+tol)
                                    eof++;
                              }
                              addblockdef(OCEAN_F2,sof,sod,eod,eof,0);
                           }
                           k++; /* bump to next sub-block. */
                        }
                        while(byt!=0x00);
                        done=1;
                     }
                  } /*---------------------------------------------------*/

                  i+=2064; /* jump to next sub-block... */
               }
               while(!done && i<tap.len);
            }
            
            else   /* No $AA, do a check for Ocean type 1... */
            {
               if(byt==0 || byt==1) /* RAM page byte will be 0 or 1. */
               {
                  i+=8;       /* skip over RAM page byte */
                  start = i;  /* store offset of load address for sub-block 1. */

                  /* ok, (definitely?) Ocean type 1...
                   this IS potentially dodgy because of the lack of flag etc...
                   but im sure it can be tightened up. */

                  /* now i rewind and add each sub block individually... */
                  k=0;
                  do
                  {
                     byt= readttbyte(start+(k*2064), ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en);
                     if(byt!=0x00)
                     {
                        if(k==0)
                           sof= ipulse;  /* use pilot start for first block */
                        else
                           sof= start+(k*2064);
                        sod= start+(k*2064);
                        eod= sod+2056;
                        eof= eod+7;

                        /* now we check to see if this is the last block, if so
                         we can trace the trailer to its end and put eof
                         there instead... */

                        if(readttbyte(eof+1, ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en)==0x00)
                        {
                           while(eof<tap.len-1 && tap.tmem[eof+1]>ft[OCEAN_F1].sp-tol && tap.tmem[eof+1]<ft[OCEAN_F1].sp+tol)
                              eof++;
                        }
                        addblockdef(OCEAN_F1,sof,sod,eod,eof,0);
                     }
                     k++;  /* move to next sub-block. */
                  }
                  while(byt!=0x00);
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
int ocean_describe(int row)
{
   int s,i,b,hd[2];

   s= blk[row]->p2;
   for(i=0;i<2; i++)
      hd[i] = readttbyte(s+(i*8), ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en);

   blk[row]->cs = (int)hd[0]<<8;
   blk[row]->ce = blk[row]->cs + 255;
   blk[row]->cx = 256;

   /* get pilot/trailer lengths... */
   blk[row]->pilot_len= blk[row]->p2- blk[row]->p1 -9;

   if(blk[row]->lt==OCEAN_F2 || blk[row]->lt==OCEAN_F3)
      blk[row]->pilot_len-=8;    /* adjust for $AA flag on F2 and F3. */
   if(blk[row]->pilot_len<0)
      blk[row]->pilot_len=0;

   blk[row]->trail_len= blk[row]->p4- (blk[row]->p3+7);


   /* extract data... */
   s= (blk[row]->p2)+8;

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);
   
   for(i=0; i<blk[row]->cx; i++)
   {
      b= readttbyte(s+(i*8), ft[OCEAN_F1].lp, ft[OCEAN_F1].sp, ft[OCEAN_F1].tp, ft[OCEAN_F1].en);
      if(b==-1)
         blk[row]->rd_err++;
      blk[row]->dd[i]=b;
   }
   return 0;
}

