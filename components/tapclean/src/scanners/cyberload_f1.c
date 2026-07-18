/*---------------------------------------------------------------------------
  cyberload_f1.c

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


  Thresholds and Pulsewidths...

  Program                    Thres         Short        Long

  *Any*                      >$39 <$45     $30          $5A
  *Any*                      >$44 <$52     $3B          $72
  Sanxion                     $2B          $24          $40
  Image Sytem, Gangster.      $79          $55          $A5

---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*---------------------------------------------------------------------------
*/
void cyberload_f1_search(void)
{
   int a,y,off;
   int i,k,z, sof,sod,eod,eof;
   int b,t, zpload,len,done;
   float ftmp;
   int sp,tp,lp,pv,sv,en;
   int *buf,bufsz;
   char info[2048];
   FILE *fp;

   /* First I must find out LOADER1 variables from the CBM HEADER... */

   strcpy(info,"");  /* all loader findings go to 'info[]'. */

   t= find_decode_block(CBM_HEAD,1);

   if(t!=-1 && tap.cbmid==LID_CYBER)
   {
      /* make an 'int' copy of the 1st CBM header file... */
      bufsz= blk[t]->cx;
      buf= (int*)malloc(bufsz*sizeof(int));
      for(i=0; i<bufsz; i++)
         buf[i]= blk[t]->dd[i];

      /* run the Cyberload Loader 1 decipher code on the CBM header...

      Note: ACCUMULATOR=3

      02DC  A0 AB          LDY #$AB
      02DE  59 50 03       EOR $0350,Y
      02E1  99 50 03       STA $0350,Y
      02E4  88             DEY
      02E5  D0 F7          BNE $02DE

      Translated to 'C' code...  */

      a = 3;
      y = 171;                  /* 171 = $AB. */
      do
      {
         off = 20 + y;          /* index 20 is equivalent to $0350. */
         a = a ^ buf[off];      /* on 1st loop 'off' = 191 (equivalent to $03FB, the last byte of the file). */
         buf[off] = a;          /* store decrypted value. */
         y--;
      }
      while(y>0);

      /* now we can read our needed variables from the deciphered Loader 1... */

      tp= (buf[33]<<8) + buf[28];
      ftmp= (float) (tp * 0.123156);
      tp= (int)ftmp;
      pv= buf[47];
      sv= buf[58];
      en= ft[CYBER_F1].en;

      if(tp>0x39 && tp<0x45)   /* pulse set A... */
      {sp=0x30;lp=0x5A;}
      if(tp>0x44 && tp<0x52)   /* pulse set B... */
      {sp=0x3B;lp=0x72;}
      if(tp>=0x2B && tp<=0x2C) /* pulse set C (sanxion)... */
      {sp=0x24;lp=0x40;}
      if(tp==0x79)             /* pulse set D {image system & gangster)... */
      {sp=0x55;lp=0xA5;}


      ft[CYBER_F1].tp= tp;   /* set variables for F1... */
      ft[CYBER_F1].sp= sp;
      ft[CYBER_F1].lp= lp;
      ft[CYBER_F1].pv= pv;
      ft[CYBER_F1].sv= sv;

      ft[CYBER_F2].tp= tp;   /* + set as default for F2... */
      ft[CYBER_F2].sp= sp;
      ft[CYBER_F2].lp= lp;
      ft[CYBER_F2].pv= pv;
      ft[CYBER_F2].sv= sv;

      ft[CYBER_F3].tp= tp;   /* + set as default for F3... */
      ft[CYBER_F3].sp= sp;
      ft[CYBER_F3].lp= lp;
      ft[CYBER_F3].pv= pv;
      ft[CYBER_F3].sv= sv;

      ft[CYBER_F4_1].tp= tp;   /* + set as default for F4.1... */
      ft[CYBER_F4_1].sp= sp;
      ft[CYBER_F4_1].lp= lp;
      ft[CYBER_F4_1].pv= pv;
      ft[CYBER_F4_1].sv= sv;

      ft[CYBER_F4_2].tp= tp;   /* + set as default for F4.2... */
      ft[CYBER_F4_2].sp= sp;
      ft[CYBER_F4_2].lp= lp;
      ft[CYBER_F4_2].pv= pv;
      ft[CYBER_F4_2].sv= sv;

      ft[CYBER_F4_3].tp= tp;   /* + set as default for F4.3... */
      ft[CYBER_F4_3].sp= sp;
      ft[CYBER_F4_3].lp= lp;
      ft[CYBER_F4_3].pv= pv;
      ft[CYBER_F4_3].sv= sv;

      sprintf(lin,"\nCyberload F1 variables have been found and set...");
      strcat(info,lin);
      sprintf(lin,"\ntp:$%02X sp:$%02X lp:$%02X pv:$%02X sv:$%02X", tp,sp,lp,pv,sv);
      strcat(info,lin);
      if(!quiet)
         msgout(info);
      strcpy(info,"");


      /* save the file so I can check it */
      if(exportcyberloaders)
      {
         sprintf(lin,"%s_Cyberloader 1.prg",tap.name);
         fp = fopen(lin,"w+b");
         fputc(blk[t]->cs & 0xFF,fp);
         fputc((blk[t]->cs & 0xFF00)>>8,fp);
         for(i=0; i<bufsz; i++)
            fputc(buf[i] & 0xFF, fp);
         fclose(fp);
      }

      free(buf);
   }


   /*----------------------------------------------------------------------*/
   if(ft[CYBER_F1].sp==VV)   /* dont scan if variables remain unset. */
      return;

   if(!quiet)
      msgout("  Cyberload F1");

   for(i=20; i<tap.len-50; i++)      /* find all format 1 files... */
   {
      if((z=find_pilot(i,CYBER_F1))>0)
      {
         sof=i;
         i=z;
         if(readttbyte(i,lp,sp,tp,en)==sv)
         {
            sod=i+8;
            b= readttbyte(sod,lp,sp,tp,en);  /* read first byte of block. */
            if(b==0x2D) /* its FORMAT1. */
            {
               zpload=0xFFD5;  /* set initial load address base. */
               done=0;
               do
               {
                  k=8;
                  len=0;
                  b= readttbyte(sod,lp,sp,tp,en);  /* read 1st byte (load offset) */
                  zpload= (zpload+b) & 0xFFFF;     /* and record it. */

                  /* get block length... */
                  /* scan all bytes (9bpb) til we find the extra bit cleared... */
                  while(tap.tmem[sod+k+8]>lp-tol && tap.tmem[sod+k+8]<lp+tol)
                  {
                     k+=9;
                     len++;
                  }
                  /* (sod+k+8)  now points to the "stack" byte (9bpb, extra bit is clear) */

                  eod= sod+k;
                  eof= eod+9;   /* +9 accounts for "is more blocks?" bit too. */

                  /* if next bit is clear then this is the last file  */
                  /* we can adjust the 'eof' to include final byte $00... */
                  if(tap.tmem[eof]>sp-tol && tap.tmem[eof]<sp+tol)
                     eof+=8;

                  addblockdef(CYBER_F1, sof,sod,eod,eof, zpload);

                  /* if next bit is set then another block follows... */
                  if(tap.tmem[eof]>lp-tol && tap.tmem[eof]<lp+tol)
                  {
                     sof= eof+1;   /* set up start of next file.. */
                     sod= sof;
                     zpload+= len;
                  }
                  else
                     done=1;
               }
               while(!done);

               i=eof; /* optimize search. */
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
int cyberload_f1_describe(int row)
{
   int i,s,b,tmp,rd_err=0;

   blk[row]->cs= blk[row]->xi;
   blk[row]->cx= ((blk[row]->p3- blk[row]->p2)-8) /9;
   blk[row]->ce= blk[row]->cs + blk[row]->cx -1;

   /* decode entire block to dd pointer... (9bpb) */
   s = blk[row]->p2+8;   /* +8 skips the $2D */

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

   for(i=0; i< blk[row]->cx; i++)
   {
      b = readttbyte(s+(i*9), ft[CYBER_F1].lp, ft[CYBER_F1].sp, NA, ft[CYBER_F1].en);
      if(b!=-1)
         blk[row]->dd[i] = b;
      else
         rd_err++;
   }
   blk[row]->rd_err = rd_err;

   tmp = readttbyte(blk[row]->p3, ft[CYBER_F1].lp, ft[CYBER_F1].sp, NA, ft[CYBER_F1].en);
   sprintf(lin,"\n - Stack byte : $%02X", tmp);
   strcat(info,lin);

   /* display trailing byte if it exists... */
   if((blk[row]->p4 - blk[row]->p3) > 9)
   {
      tmp = readttbyte((blk[row]->p3)+9, ft[CYBER_F1].lp, ft[CYBER_F1].sp, NA, ft[CYBER_F1].en);
      sprintf(lin,"\n - Trailing byte : $%02X", tmp);
      strcat(info,lin);
   }

   /* get pilot & trailer lengths... */
   blk[row]->pilot_len= (blk[row]->p2- blk[row]->p1) >>3;
   blk[row]->trail_len= 0;
   if(blk[row]->pilot_len > 0)
      blk[row]->pilot_len--;    /* if there IS pilot then disclude the sync byte. */

   return 0;
}

