/*---------------------------------------------------------------------------
  cyberload_f2.c

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


  note: for Vendetta etc the 'Cyberloader_4.3' program is NOT held in an F2 file,
  it is contained in F3's instead!.

---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*---------------------------------------------------------------------------
*/
int cyber_f2_eor1 = 0xAE;	/* default XOR codes for cyberload f2.. */
int cyber_f2_eor2 = 0xD2;

void cyberload_f2_search(void)
{
   int i,z, sof,sod,eod,eof;
   unsigned char t1,t2;
   int tp,sp,lp,pv,sv;
   int b,t;
   float ftmp;
   int *buf,bufsz;
   char info[2048];

   int a,x,y, k,encstart;

   FILE *fp;

   /* loader 2: pattern match for deciphering program. */
   int decryptor[7]= {0x59,0x14,0x03,0x88,0x10,0xFA,0x9D};

   /* loader 2: deciphering data. (as found at $0314) */
   int decryptorcodes[16] = {0xA6,0x02,0x6B,0x8E,0xC1,0xFE,0xBC,0x8D,
                             0x8B,0x49,0x7F,0x8D,0x60,0xD1,0x8D,0xE1};

   /* loader 2/3/4: set threshold pattern. */
   int set_thres[10]={0xA9,XX,0x8D,0x04,0xDC,0xA9,XX,0x8D,0x05,0xDC};

   /* loader 2/3/4: set pilot/sync pattern. */
   int set_pilot[7]= {0xC9,XX,0xF0,XX,0xC9,XX,0xD0};


   /* First find out F2 variables from the loader in the 1st CYBER_F1... */

   strcpy(info,"");  /* all loader findings go to 'info[]'. */

   t= find_decode_block(CYBER_F1,1);
   if(t!=-1 && blk[t]->xi==0x0002)      /* the loader2 file. */
   {
      /* make an 'int' copy of the data... */
      bufsz= blk[t]->cx;

      /* The buffer allocated below is too small (causes buffer overrun(s))
         without the +100 (on ie. Last Ninja), why???...

         Answer:-

         The original loader2 decryptor indexes beyond the EOF too, its not a
         problem when indexing the C64's RAM a little further into zero page
         but we have to make the buffer here a bit larger to allow for it. */

      buf= (int*)malloc((200+bufsz)*sizeof(int));
      for(i=0; i<bufsz; i++)
         buf[i]= blk[t]->dd[i];

      k= find_seq(buf,bufsz, decryptor,7);  /* find deciphering code.. */
      if(k!=-1)
      {
         /* perform the decipher... */

         encstart = buf[k-2]-0x0002;   /* buffer index of encrypted data start */
         x = buf[k-8];

         do
         {
            a = x & 0x0F;
            y = a;
            a = a ^ buf[encstart + x];

            /* IMPORTANT NOTE : index (encstart + x) may be beyond
               the end of the file!, obviously not a problem when indexing
               the C64's RAM.
               I make the buffer (buf) a bit larger to allow for it. */

            do
            {
               a = a ^ decryptorcodes[y--];
            }
            while(y>-1);

            buf[encstart + x] = a;
            x--;
         }
         while(x>0);
      }
      else
         msgout("\nWarning: Cyberload F2 scanner did not find loader 2 decipher program. (possibly it is not required. ie. Sanxion).\n");


      /*--------------------------------------------------------------
       search for loader2 'set pilot/sync' pattern...
       note : sanxion finds this too. */

      i= find_seq(buf,bufsz, set_pilot,7);
      if(i!=-1)
      {
         pv= buf[i+1];
         sv= buf[i+5];

         cyber_f2_eor1=0xAE;   /* just some defaults, usually overwritten later.. */
         cyber_f2_eor2=0xD2;

         /* this bit handles sanxion.. eor code 1 is 3D, eor code 2 is N/A. */
         if(buf[i+25]==0xA0)  /* $A0 = LDY, other Loader 2's have EOR ($49) */
         {
            cyber_f2_eor1= buf[i+14];   /* code is $3D in my copy of Sanxion. */
            cyber_f2_eor2= 0;
         }
      }
      else
         msgout("\nWarning: Cyberload F2 scanner did not find loader 2 'set pilot & sync' pattern.\n");


      /*--------------------------------------------------------------
       search for loader2 'set threshold' pattern...
       note : sanxion finds this too. */

      i= find_seq(buf,bufsz, set_thres,10);
      if(i!=-1)
      {
         tp= (buf[i+6]<<8) + buf[i+1];  /* +6=high +1=low */
         ftmp= (float) (tp*0.123156);
         tp= (int)ftmp;

         if(tp>0x39 && tp<0x45)   /* pulse set A... */
         {sp=0x30;lp=0x5A;}
         if(tp>0x44 && tp<0x52)   /* pulse set B... */
         {sp=0x3B;lp=0x72;}
         if(tp>=0x2B && tp<=0x2C) /* pulse set C (sanxion)... */
         {sp=0x24;lp=0x40;}
         if(tp==0x79)             /* pulse set D {image system & gangster)... */
         {sp=0x55;lp=0xA5;}


         ft[CYBER_F2].tp= tp;   /* set variables for F2... */
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

         if(cyber_f2_eor2!=0)     /* not looking at Sanxion?... */
         {
            cyber_f2_eor1= buf[i+39];   /* set EOR codes... */
            cyber_f2_eor2= buf[i+51];
         }

         sprintf(lin,"\nCyberload F2 variables have been found and set...");
         strcat(info,lin);
         sprintf(lin,"\ntp:$%02X sp:$%02X lp:$%02X pv:$%02X sv:$%02X ", tp,sp,lp,pv,sv);
         strcat(info,lin);
         sprintf(lin,"\nXOR code 1:$%02X, XOR code 2:$%02X.", cyber_f2_eor1, cyber_f2_eor2);
         strcat(info,lin);
         if(!quiet)
            msgout(info);
         strcpy(info,"");
      }
      else
         msgout("\nWarning: Cyberload F2 scanner did not find loader 2 'set threshold' pattern.\n");


      /* save the file so I can check it */
      if(exportcyberloaders)
      {
         sprintf(lin,"%s_Cyberloader 2.prg",tap.name);
         fp = fopen(lin,"w+b");
         fputc(0x02,fp);
         fputc(0x00,fp);
         for(i=0; i<bufsz; i++)
            fputc(buf[i] & 0xFF, fp);
         fclose(fp);
      }

      free(buf);
   }


   /*------------------------------------------------------------------------
    scan...  */

   if(ft[CYBER_F2].sp ==VV)   /* dont scan if variables remain unset. */
      return;

   if(!quiet)
      msgout("  Cyberload F2");

   strcpy(info,"");

   for(i=20; i<tap.len-50; i++)
   {
      if((z=find_pilot(i,CYBER_F2))>0)
      {
         sof=i;
         i=z;
         if(readttbyte(i, ft[CYBER_F2].lp, ft[CYBER_F2].sp, ft[CYBER_F2].tp, ft[CYBER_F2].en)==ft[CYBER_F2].sv)
         {
            sod=i+8;  /* sod points to byte following sync. */

            b= readttbyte(sod, ft[CYBER_F2].lp, ft[CYBER_F2].sp, ft[CYBER_F2].tp, ft[CYBER_F2].en);

             /* (Note: $2D in 1st byte = Cyberload Format 1)
               so its FORMAT2 (will contain LOADER3 or LOADER4)... */

            if(b!=0x2D)
            {

               /* get size (using eor code 1 found from loader2's (loader 1) file)... */
               t1= readttbyte(sod+16, ft[CYBER_F2].lp, ft[CYBER_F2].sp, ft[CYBER_F2].tp, ft[CYBER_F2].en) ^ cyber_f2_eor1;
               t2= readttbyte(sod+24, ft[CYBER_F2].lp, ft[CYBER_F2].sp, ft[CYBER_F2].tp, ft[CYBER_F2].en) ^ cyber_f2_eor1;
               x= t1+(t2<<8);

               eod= sod+((x+4)*8) -8;
               eof= eod+7;

               /* include trailing byte ($00) if present.. */
               if(readttbyte(eof+1, ft[CYBER_F2].lp, ft[CYBER_F2].sp, ft[CYBER_F2].tp, ft[CYBER_F2].en)==0)
                  eof+=8;
               addblockdef(CYBER_F2, sof,sod,eod,eof,0);
               i= eof;
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
int cyberload_f2_describe(int row)
{
   int i,s,b,tmp,rd_err=0;
   unsigned char hd[32];

   /* decode the first 4... */
   s = blk[row]->p2;
   for(i=0; i<4; i++)
      hd[i] = readttbyte(s+(i*8), ft[CYBER_F2].lp, ft[CYBER_F2].sp, ft[CYBER_F2].tp, ft[CYBER_F2].en);

   blk[row]->cs = (hd[0]^cyber_f2_eor1)+((hd[1]^cyber_f2_eor1)<<8);
   blk[row]->cx = (hd[2]^cyber_f2_eor1)+((hd[3]^cyber_f2_eor1)<<8);
   blk[row]->ce = (blk[row]->cs + blk[row]->cx)-1;

   /* decode entire block... */
   s = blk[row]->p2+(4*8);  /* 4*8 skips header */

   if(blk[row]->dd!=NULL)   /* note : may have been already decoded by a call to "find_decode_block()". */
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

   for(i=0; i< blk[row]->cx; i++)
   {
      b = readttbyte(s+(i*8), ft[CYBER_F2].lp, ft[CYBER_F2].sp, ft[CYBER_F2].tp, ft[CYBER_F2].en);
      if(b!=-1)
         blk[row]->dd[i] = b ^cyber_f2_eor2;  /* export as deciphered. */
      else
         rd_err++;
   }
   blk[row]->rd_err = rd_err;

   /* display trailing byte if it exists... */
   if((blk[row]->p4 - blk[row]->p3) > 8)
   {
      tmp = readttbyte((blk[row]->p3)+8, ft[CYBER_F2].lp, ft[CYBER_F2].sp, ft[CYBER_F2].tp, ft[CYBER_F2].en);
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




