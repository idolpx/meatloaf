/*---------------------------------------------------------------------------
  cyberload_f3.c

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
#include <stdlib.h>
#include <stdio.h>

#define HDSZ 8

/*---------------------------------------------------------------------------
*/
void cyberload_f3_search(void)
{
   int i,sof,sod,eod,eof,z,x,sz;
   int *buf,bufsz;
   int tp,sp,lp,pv,sv;
   int location,j,done,done2,t;
   float ftmp;
   char info[2048];
   FILE *fp;

   /* loader 2/3/4: set threshold pattern. */
   int set_thres[10]={0xA9,XX,0x8D,0x04,0xDC,0xA9,XX,0x8D,0x05,0xDC};

   /* loader 2/3/4: set pilot/sync pattern. */
   int set_pilot[7]= {0xC9,XX,0xF0,XX,0xC9,XX,0xD0};




   /* First I find out F3 variables from the loader in the 1st F2 file (Loader3)... */

   strcpy(info,"");  /* all loader findings go to 'info[]'. */

   t= find_decode_block(CYBER_F2,1);
   if(t!=-1)
   {
      /* make an 'int' copy of the data... */
      bufsz= blk[t]->cx;
      buf= (int*)malloc(bufsz*sizeof(int));
      for(i=0; i<bufsz; i++)
         buf[i]= blk[t]->dd[i];

      /* search for loader3 'set threshold' code... */
      done=0;
      j= find_seq(buf,bufsz, set_thres,10);
      if(j!=-1)
      {
         location=j;
         done=1;
      }

      if(done)  /* search was a success, set threshold variables... */
      {
         tp= (buf[location+6]<<8) + buf[location+1];  /* +1=low, +6=high */
         ftmp= (float) (tp*0.123156);
         tp= (int)ftmp;

         if(tp>0x39 && tp<0x45)   /* pulse set A... */
         {sp=0x30;lp=0x5A;}
         if(tp>0x44 && tp<0x52)   /* pulse set B... */
         {sp=0x3B;lp=0x72;}
         if(tp>=0x2C && tp<=0x33) /* pulse set C (sanxion)... */
         {sp=0x24;lp=0x40;}
         if(tp==0x79)             /* pulse set D {image system & gangster)... */
         {sp=0x55;lp=0xA5;}
      }

      /* search for loader3 "set pilot and sync" code... */
      done2=0;
      j= find_seq(buf,bufsz, set_pilot,7);
      if(j!=-1)
      {
         location=j;
         done2=1;
      }

      if(done && done2)  /* both searches were a success!... */
      {
         pv= buf[location+1];
         sv= buf[location+5];

         ft[CYBER_F3].tp= tp;     /* set variables for F3 */
         ft[CYBER_F3].sp= sp;
         ft[CYBER_F3].lp= lp;
         ft[CYBER_F3].pv= pv;
         ft[CYBER_F3].sv= sv;

         ft[CYBER_F4_1].pv= pv;  /* incase f4 type is 4_3. */
         ft[CYBER_F4_2].pv= pv;
         ft[CYBER_F4_3].pv= pv;

         sprintf(lin,"\nCyberload F3 variables have been found and set...");
         strcat(info,lin);
         sprintf(lin,"\ntp:$%02X sp:$%02X lp:$%02X pv:$%02X sv:$%02X", tp,sp,lp,pv,sv);
         strcat(info,lin);

         if(!quiet)
            msgout(info);
         strcpy(info,"");
      }

      /* save the file so I can check it */
      if(exportcyberloaders)
      {
         sprintf(lin,"%s_Cyberloader 3.prg",tap.name);
         fp = fopen(lin,"w+b");
         fputc(blk[t]->cs & 0xFF,fp);
         fputc((blk[t]->cs & 0xFF00)>>8,fp);
         for(i=0; i<bufsz; i++)
            fputc(buf[i] & 0xFF, fp);
         fclose(fp);
      }

      free(buf);
   }



   /*---------------------------------------------------------------------------------
    scan... */

   if(ft[CYBER_F3].sp ==VV)   /* don't scan if variables remain unset. */
      return;

   if(!quiet)
      msgout("  Cyberload F3");


   for(i=20; i<tap.len-50; i++)
   {
      if((z=find_pilot(i,CYBER_F3))>0)
      {
         sof=i;
         i=z;
         if(readttbyte(i, ft[CYBER_F3].lp, ft[CYBER_F3].sp, ft[CYBER_F3].tp, ft[CYBER_F3].en) ==ft[CYBER_F3].sv)
         {
            sod=i+8;
            x=readttbyte(i+24, ft[CYBER_F3].lp, ft[CYBER_F3].sp, ft[CYBER_F3].tp, ft[CYBER_F3].en);  /* read size byte in header. */
            if(x==0)
               x=256;
            eod=i+(x*8)+(HDSZ*8);
            eof=eod+7;

            sz= ((eod-sod)>>3)-7;  /* check data size */
            if(sz>0)
               addblockdef(CYBER_F3, sof,sod,eod,eof,0);
            i=eof;
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
int cyberload_f3_describe(int row)
{
   int i,s,b,cb;
   unsigned char hd[HDSZ];

   /* decode the header... */
   s= blk[row]->p2;
   for(i=0; i<HDSZ; i++)
      hd[i]= readttbyte(s+(i*8), ft[CYBER_F3].lp, ft[CYBER_F3].sp, ft[CYBER_F3].tp, ft[CYBER_F3].en);

   blk[row]->cs= hd[0]+(hd[1]<<8);                            /* record start address */
   blk[row]->cx= ((blk[row]->p3 - blk[row]->p2)>>3)-7;        /* record length */
   blk[row]->ce= ((blk[row]->cs + blk[row]->cx) & 0xFFFF)-1;  /* record end address */

   if (hd[4] & 0x80)
   {
      sprintf(lin, "\n - Exe Address : $%04X", hd[6] + (hd[7] << 8));
      strcat(info, lin);
   }

   /* get pilot & trailer lengths... */
   blk[row]->pilot_len= (blk[row]->p2- blk[row]->p1) >>3;
   blk[row]->trail_len= 0;
   if(blk[row]->pilot_len > 0)
      blk[row]->pilot_len--;    /* if there IS pilot then disclude the sync byte. */

   /* extract data and test checksum... */
   s= blk[row]->p2;
   cb=0;

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd= (unsigned char*)malloc(blk[row]->cx);

   for(i=0; i< blk[row]->cx+HDSZ; i++)
   {
      b= readttbyte(s+(i*8), ft[CYBER_F3].lp, ft[CYBER_F3].sp, ft[CYBER_F3].tp, ft[CYBER_F3].en);
      if(b==-1)
         blk[row]->rd_err++;
      else
      {
         cb^=b;
         if(i>=HDSZ)                   /* (begin prg extract after header) */
            blk[row]->dd[i-HDSZ]= b;
      }
   }
   blk[row]->cs_exp= cb;
   blk[row]->cs_act= 0;

   /* Ignore $04 checksum fault?.. */
   if(docyberfault==FALSE && blk[row]->cs_exp == 0x04)
      blk[row]->cs_exp=0x00;

   return 0;
}



