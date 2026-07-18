/*---------------------------------------------------------------------------
  cyberload_f4.c

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


Cyberload F4.3

Game          |  Pilot | Sync | ID

Last Ninja 3  |  $EC   | $96  | $55
Vendetta      |  $73   | $96  | $55
Hammerfist    |  $0F   | $99  | $AA

---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*---------------------------------------------------------------------------
*/
void cyberload_f4_search(void)
{
   int i,sof,sod,eod,eof,z,m,loaderset=0;
   int b,ih,dx,tmp;
   int checkbyt;
   int l4_type=0, l4_offset_to_filename, l4_hsize;
   unsigned char hd[32];
   int *buf,bufsz;
   int tp,sp,lp,pv,sv;
   int location,j,done,done2,t;
   float ftmp;
   FILE *fp;

   /* loader 2/3/4: set threshold pattern. */
   int set_thres[10]={0xA9,XX,0x8D,0x04,0xDC,0xA9,XX,0x8D,0x05,0xDC};

   /* loader 2/3/4: set pilot/sync pattern. */
   int set_pilot[7]= {0xC9,XX,0xF0,XX,0xC9,XX,0xD0};

   char info[2048];


   /* First I find out F4 variables from the loader in the 2nd F2 file... */

   strcpy(info,"");  /* all loader findings go to 'info[]'. */

   t= find_decode_block(CYBER_F2,2);
   if(t!=-1)
   {
      /* make an 'int' copy of the data... */
      bufsz= blk[t]->cx;
      buf= (int*)malloc(bufsz*sizeof(int));
      for(i=0; i<bufsz; i++)
         buf[i]= blk[t]->dd[i];

      /* search for loader3/4 'set threshold' code... */
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
         if(tp==0x2B)             /* pulse set C (sanxion)... */
         {sp=0x24;lp=0x40;}
         if(tp==0x79)             /* pulse set D {image system & gangster)... */
         {sp=0x55;lp=0xA5;}
      }

      /* search for loader3/4 "set pilot and sync" code... */
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

         ft[CYBER_F4_1].tp= tp;
         ft[CYBER_F4_1].sp= sp;
         ft[CYBER_F4_1].lp= lp;
         ft[CYBER_F4_1].pv= pv;
         ft[CYBER_F4_1].sv= sv;

         ft[CYBER_F4_2].tp= tp;
         ft[CYBER_F4_2].sp= sp;
         ft[CYBER_F4_2].lp= lp;
         ft[CYBER_F4_2].pv= pv;
         ft[CYBER_F4_2].sv= sv;

      /* ft[CYBER_F4_3].tp= tp;     4.3 variables are not set by this a 2nd F2 file.
         ft[CYBER_F4_3].sp= sp;
         ft[CYBER_F4_3].lp= lp;
         ft[CYBER_F4_3].pv= pv;
         ft[CYBER_F4_3].sv= sv;  */

         sprintf(lin,"\nCyberload F4 variables have been found and set...");
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
         sprintf(lin,"%s_Cyberloader 4.prg",tap.name);
         fp = fopen(lin,"w+b");
         fputc(blk[t]->cs & 0xFF,fp);
         fputc((blk[t]->cs & 0xFF00)>>8,fp);
         for(i=0; i<bufsz; i++)
            fputc(buf[i] & 0xFF, fp);
         fclose(fp);
      }

      free(buf);
   }
   /* else..  cyberload loader 4 was not found (ie. no 2nd format 2 file).
      Note: not all cyberload multiloaders require it. F4.3 does not i think. */



   /*---------------------------------------------------------------------------------
     Scan... */

   if(ft[CYBER_F4_1].sp ==VV)   /* dont scan if variables remain unset. */
      return;

   if(!quiet)
      msgout("  Cyberload F4 (3 types)");


   for(i=20; i<tap.len-50; i++)
   {
      if((z=find_pilot(i,CYBER_F4_1))>0)
      {
         sof=i;
         i=z;
         b= readttbyte(i, ft[CYBER_F4_1].lp, ft[CYBER_F4_1].sp, NA, MSbF);
         if(b==0x96 || b==0xAA || b==0x99 || b==0x55)  /* cyber_f4_sync */
         {
            sod=i+8;

            /* decode header to get end locations... */
            for(ih=0; ih<32; ih++)
               hd[ih]= readttbyte(sod+(ih*8), ft[CYBER_F4_1].lp, ft[CYBER_F4_1].sp, NA, MSbF);

            /* test header checksum (4.1 & 4.2) to verify format4 subtype... */

            if(loaderset==0)
            {
               m=0;

               checkbyt=0;
               for(j=0; j<20; j++)
                  checkbyt^=hd[j];

               if(hd[j]==checkbyt)   /* note : this fails for atomic robokid b side and at least 1 other tape. */
               {
                  l4_type=1;
                  m++;
                  loaderset=1;
               }

               checkbyt^=hd[j];    /* XOR in the next 2 bytes.. */
               checkbyt^=hd[j+1];
               if(hd[j+2]== checkbyt)
               {
                  l4_type=2;
                  m++;
                  loaderset=1;
               }

               if(m==2)   /* m==2 when above tests BOTH passed (Atomic Robokid B-side) */
                  msgout("\n * Cannot determine Cyberload F4 type, using F4.2");

               /* test for cyberload f4_3... */
               checkbyt=0;
               for(j=0; j<22; j++)
                  checkbyt^=hd[1+j];
               if(hd[1+j]==checkbyt)
               {
                  l4_type=3;
                  loaderset=1;
               }
            }


            if(loaderset)     /* an F4 type HAS been selected. */
            {
               if(l4_type==1)
               {
                  l4_offset_to_filename=0;
                  l4_hsize=21;
               }
               if(l4_type==2)
               {
                  l4_offset_to_filename=0;
                  l4_hsize=24;
               }
               if(l4_type==3)
               {
                  l4_offset_to_filename=1;
                  l4_hsize=25;
               }

               /* get data size; */
               dx = hd[l4_offset_to_filename+18]+(hd[l4_offset_to_filename+19]<<8);

               /* file size =  written size + 20 (header) + 1 byte (checksum)
                  for each data packet (256 or less). */

               tmp = dx>>8;
               if(dx%256!=0)
                  tmp++;

               eod = sod+(l4_hsize +dx+tmp)*8;
               eof = eod+7;

               if(l4_type==1)
                  addblockdef(CYBER_F4_1, sof,sod,eod,eof, 0);
               if(l4_type==2)
                  addblockdef(CYBER_F4_2, sof,sod,eod,eof, 0);
               if(l4_type==3)
                  addblockdef(CYBER_F4_3, sof,sod,eod,eof, 0);
               i=sod;
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
int cyberload_f4_describe(int row)
{
   int cnt,pos,i,s,tot;
   int b,tmp,rd_err=0;
   int good,done,boff,blocks;
   int thrlo,thrhi,tmp2;
   int carry;
   int l4_offset_to_filename, l4_hsize, cs_hsize;
   unsigned char hd[32];
   char fname[17], bfnameASCII[17];
   unsigned char cb;

   if(blk[row]->lt==CYBER_F4_1)
   {
      l4_offset_to_filename=0;
      l4_hsize=21;
      cs_hsize=20;
   }
   if(blk[row]->lt==CYBER_F4_2)
   {
      l4_offset_to_filename=0;
      l4_hsize=24;
      cs_hsize=22;
   }
   if(blk[row]->lt==CYBER_F4_3)
   {
      l4_offset_to_filename=1;
      l4_hsize=25;
      cs_hsize=22;
   }

   /* decode header... */
   s= blk[row]->p2;
   for(i=0; i<l4_hsize; i++)
      hd[i] = readttbyte(s+(i*8), ft[CYBER_F4_1].lp, ft[CYBER_F4_1].sp, NA, MSbF);

   /* extract filename... */
   for(i=0; i<16; i++)
      fname[i] = hd[l4_offset_to_filename +i];
   fname[16]=0;

   trim_string(fname);
   pet2text(bfnameASCII, fname);

   if (blk[row]->fn != NULL)
      free(blk[row]->fn);
   blk[row]->fn = (char*)malloc(strlen(bfnameASCII) + 1);
   strcpy(blk[row]->fn, bfnameASCII);

   blk[row]->cs = hd[l4_offset_to_filename +16] + (hd[l4_offset_to_filename +17]<<8);
   blk[row]->cx = hd[l4_offset_to_filename +18] + (hd[l4_offset_to_filename +19]<<8);
   blk[row]->ce = blk[row]->cs + blk[row]->cx -1;

   /* compute "encrypted threshold to load data" if 4.2 or 4.3... */
   if(blk[row]->lt==CYBER_F4_2 || blk[row]->lt==CYBER_F4_3)
   {
      thrlo =  hd[l4_offset_to_filename +20];
      thrhi =  hd[l4_offset_to_filename +21];

      sprintf(lin,"\n - Enciphered Threshold : $%02X%02X",thrhi,thrlo);
      strcat(info,lin);

      /* high byte is shifted right thus affecting the carry flag... */
      carry=0;
      tmp2 = thrhi;
      if(tmp2&1)
         carry=1;
      tmp2 = tmp2>>1;
      /* then low byte is rotated right and, after clearing Carry, it is added
         to its starting value.. */
      tmp = (thrlo>>1) + thrlo;
      if(carry==1)
         tmp|=128;
      thrlo = tmp;
      /*Now, without clearing Carry, the previously shifted high byte is added
        to its starting value. */
      thrhi = tmp2+thrhi;

      sprintf(lin,"\n - Deciphered Threshold : $%02X%02X",thrhi,thrlo);
      strcat(info,lin);
   }

   tmp = (blk[row]->cx)>>8;
   if((blk[row]->cx)%256!=0)
      tmp++;
   sprintf(lin,"\n - Sub-blocks : %d",tmp);
   strcat(info,lin);

   /* get pilot & trailer lengths... */
   blk[row]->pilot_len= (blk[row]->p2- blk[row]->p1) >>3;
   blk[row]->trail_len= 0;
   if(blk[row]->pilot_len > 0)
      blk[row]->pilot_len--;    /* if there IS pilot then disclude the sync byte. */

   /* test the header checkbyte... */
   s= blk[row]->p2 + (l4_offset_to_filename*8);
   for(cb=0,i=0; i<cs_hsize; i++)
   {
      b= readttbyte(s+(i*8), ft[CYBER_F4_1].lp, ft[CYBER_F4_1].sp, NA, MSbF);
      cb^=b;
   }
   b= readttbyte(s+(i*8), ft[CYBER_F4_1].lp, ft[CYBER_F4_1].sp, NA, MSbF);
   if(b==cb)
   {
      sprintf(lin,"\n - Header checkbyte OK (expected=$%02X, actual=$%02X)",cb,b);
      strcat(info,lin);

   }
   else
   {
      sprintf(lin,"\n - Header checkbyte FAILED (expected=$%02X, actual=$%02X)",cb,b);
      strcat(info,lin);
   }

   /* now test all sub-block checksums individually... */
   s= blk[row]->p2+((l4_hsize)*8);
   pos= s;
   good= 0;
   blocks= 0;
   do
   {
      cnt=0;
      done=0;
      boff = blocks*257*8;
      cb=0;
      do
      {
         b= readttbyte(pos+boff+(cnt*8), ft[CYBER_F4_1].lp, ft[CYBER_F4_1].sp, NA, MSbF);
         cb^=b;
         cnt++;
         if(cnt==256 || pos+boff+(cnt*8)==blk[row]->p3) /* we reached the checkbyte (257th) */
         {
            b= readttbyte(pos+boff+(cnt*8), ft[CYBER_F4_1].lp, ft[CYBER_F4_1].sp, NA, MSbF);
            if(b==cb)
               good++;
            blocks++;  /* counts blocks done */
            done=1;
         }
      }
      while(!done);
   }
   while(pos+boff+(cnt*8) < blk[row]->p3-8);

   sprintf(lin,"\n - Verified sub-block checkbytes : %d of %d",good,blocks);
   strcat(info,lin);
   blk[row]->cs_exp=blocks;
   blk[row]->cs_act=good;

   /* decode all sub-blocks as one... */
   s= (blk[row]->p2)+(l4_hsize*8);

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

   for(i=0,tot=0; tot<blk[row]->cx;)
   {
      b= readttbyte(s+(i*8), ft[CYBER_F4_1].lp, ft[CYBER_F4_1].sp, NA, MSbF);
      if(b!=-1)
         blk[row]->dd[tot]= b;
      else
      {
         blk[row]->dd[tot] = 0x69;	/* sentinel error value */
         rd_err++;
      }
      tot++;
      i++;
      if(i==256)
      {
         i=0;
         s+=(257*8); /* skip to next sub-block */
      }
   }
   blk[row]->rd_err= rd_err;
   return(rd_err);
}


