/*---------------------------------------------------------------------------
  superpav.c (Super Pavloda)

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
   

  Note: Handles Both known threshold types, SPAV1 and SPAV2

---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*---------------------------------------------------------------------------
 note: set mode==1 to reset (no read performed)
*/
int superpav_readbyte(int sp, int mp, int lp, int pos, int mode)
{
   /* status 1 lookup table... */
   int tab1[3][2] = {{1, 1},    /* S (1 bit)  */
                     {2, 00},   /* M (2 bits) */
                     {2, 01}};  /* L (2 bits) */

   /* status 2 lookup table... */
   int tab2[3][2] = {{1, 0},    /* S (1 bit) */
                     {1, 1},    /* M (1 bits) */
                     {2, 00}};  /* L (2 bits)  this is never used. */

   int p,pcnt,a,bits,val,sw,byt;
   static int status=1, bpos=0, bbuf=0;

   if(mode==1)   /* reset reader. */
   {
      status=1;
      bpos=0;
      bbuf=0;
      return 0;
   }

   if(pos>(tap.len-8) || pos<20) /* return -1 if out of bounds. */
      return(-1);
   if(is_pause_param(pos))
      return(-1);

   pcnt=0;  /* counts the number of pulses used for this byte. */

   do
   {
      p = tap.tmem[pos];

      a=-1;  /* we can test read errors by presetting this to error code. */

      if(p>(sp-tol) && p<(sp+tol))  /* SHORT. */
      { a=0; sw=0; }
      if(p>(mp-tol)   && p<(mp+tol))  /* MEDIUM. (switch status) */
      { a=1; sw=1; }
      if(p>(lp-tol)  && p<(lp+tol))   /* LONG. */
      { a=2; sw=0; }

      /*if(status==2 && a==2)
          Application->MessageBox("FOUND 2:2", "Oops", MB_OK);   */


      if(a==-1)
         break;  /* read error!. */

      if(status==1)
      {
         bits=tab1[a][0];
         val =tab1[a][1];
         if(sw)
            status=2;
      }
      else /* status==2. */
      {
         bits= tab2[a][0];
         val =tab2[a][1];
         if(sw)
            status=1;
      }

      bbuf= bbuf | ((val<<(16-bits)) >>bpos);
      bpos+=bits;                 /* bump bit counter. */

      pos++;  /* bump file offset. */
      pcnt++;
   }
   while(bpos<8);  /* loop until we have (at least) 8 bits. */

   if(a==-1)
      return -1;  /* return error. */


   byt= (bbuf & 0xFF00)>>8;

   bbuf= (bbuf<<8) & 0xFF00;  /* slide off lower 8 bits of bbuf. */
   bpos-=8;         /* and reduce bit position by 8. */

   return(byt+(pcnt<<8));  /* return 8 bits of bbuf */
                           /* +number of pulses used. */
}

/*---------------------------------------------------------------------------
*/
void superpav_search(void)
{
   int i,sof,sod,eod,eof;
   int p,x,byt,nxt,hd[7], bcnt,sn;
   int start,si,j;
   int pass=1,sp,mp,lp,lt,ht;
   char lname[2][16] = {"T1","T2"};

   do
   {
      if(pass==1)
         {sp=ft[SPAV1].sp; mp=ft[SPAV1].mp; lp=ft[SPAV1].lp; lt=SPAV1; ht=SPAV1_HD;}  /* Pav T1 */
      if(pass==2)
         {sp=ft[SPAV2].sp; mp=ft[SPAV2].mp; lp=ft[SPAV2].lp; lt=SPAV2; ht=SPAV2_HD;}  /* Pav T2 */

      if(!quiet)
      {
         sprintf(lin,"  Super Pavloda %s", lname[pass-1]);
         msgout(lin);
            
      }
      for(i=20; i<tap.len; i++)
      {
         p= tap.tmem[i];

         if(p>(sp-tol) && p<(sp+tol) && !is_pause_param(i))  /* SHORT. */
         {
            sof=i;
            do
               p = tap.tmem[i++];
            while(p>(sp-tol) && p<(sp+tol) && !is_pause_param(i));

            if(p>(mp-tol) && p<(mp+tol) && ((i-sof)>4)) /* MEDIUM. (sync) */
            {
               superpav_readbyte(sp,mp,lp, 0,1); /* reset reader */

               x= superpav_readbyte(sp,mp,lp, i, 0);
               byt= x & 0xFF;
               nxt= (x & 0xFF00)>>8;
               i+=nxt;

               if(byt==0x66)
               {
                  x= superpav_readbyte(sp,mp,lp, i, 0);
                  byt= x & 0xFF;
                  nxt= (x & 0xFF00)>>8;
                  i+=nxt;

                  if(byt==0x1B)
                  {
                     /* sprintf(lin,"\n  sync bytes ($66,$1B) before $%04X",i); */

                     sod=i;  /* record start of data. */


                     /* to compute the end of the file offset, we must actually read
                        every byte until we reach the end (size is in header).
                        (because pavloda uses varying no. pulses per byte)  */


                     start= sod;  /* decode header... */
                     si= start;

                     /* superpav_readbyte(sp,mp,lp, 0,1);   reset reader */

                     /* decode first 2 bytes, block number and sub-block number... */
                     for(j=0; j<2; j++)
                     {
                        x= superpav_readbyte(sp,mp,lp, si, 0);     /* read a pav byte */
                        byt= x & 0xFF;                /* extract byte value. */
                        nxt= (x & 0xFF00)>>8;         /* and offset to next. */
                        hd[j]= byt;
                        si+=nxt;  /* bump si to point at offset to next byte. */
                     }

                     /* bn = hd[0];   get block number */
                     sn= hd[1];    /* get sub-block number */

                     if(sn==0)   /* is this a 1st block? */
                     {
                        /* decode remaining header bytes to hd[]... */
                        for(j=2; j<7; j++)
                        {
                           x= superpav_readbyte(sp,mp,lp, si, 0);     /* read a pav byte */
                           byt= x & 0xFF;                /* extract byte value. */
                           nxt= (x & 0xFF00)>>8;         /* and offset to next. */
                           hd[j]= byt;
                           si+=nxt;  /* bump si to point at offset to next byte. */
                        }

                        /* get total number of data bytes in whole chain. */
                        /* total_data = (hd[4]*256) +(256 -hd[5]); */

                        /* get number of data bytes in this 1st file. */
                        bcnt = 256 -hd[5];
                     }
                     else    /* not a 1st block. */
                        bcnt=256;


                     /* now read in all data bytes + checksum... */
                     /* si = start;                               */
                     /* superpav_readbyte(0,1);    reset reader   */

                     for(j=0; j<bcnt+1; j++)
                     {
                        x = superpav_readbyte(sp,mp,lp, si, 0);     /* read a pav byte */
                        /* byt = x & 0xFF;                             extract byte value. */
                        nxt = (x & 0xFF00)>>8;                      /* and offset to next. */
                        si+=nxt;  /* bump si to point at offset to next byte. */
                     }

                     eod=si;
                     eof=si;

                     if(sn==0)
                        addblockdef(ht, sof,sod,eod,eof,0);
                     else
                        addblockdef(lt, sof,sod,eod,eof,0);

                     i=si;
                  }
               }
            }
         }
      }
      pass++;
   }
   while(pass<3);   /* run 2 times */
}
/*---------------------------------------------------------------------------
*/
int superpav_describe(int row)
{
   int i,start,si, sp,mp,lp;
   int hd[7], x,byt,nxt,tmp,checkbyt;
   int rd_err=0;
       
   static int loadbase=0;

   /* each time a header is described, 'loadbase' is set to the load address
      contained in it.
      each time a sub-block is described, its load address is set according to
      the current contents of 'loadbase'

      this is the only way i can accomplish this.
   */

   if(blk[row]->lt==SPAV1 || blk[row]->lt==SPAV1_HD)
      {sp=ft[SPAV1].sp; mp=ft[SPAV1].mp; lp=ft[SPAV1].lp;}  /* Pav T1 */
   if(blk[row]->lt==SPAV2 || blk[row]->lt==SPAV2_HD)
      {sp=ft[SPAV2].sp; mp=ft[SPAV2].mp; lp=ft[SPAV2].lp;}  /* Pav T2 */


   if(blk[row]->lt==SPAV1_HD || blk[row]->lt==SPAV2_HD)   /* its a HEADER?.... */
   {
      start= blk[row]->p2;
      si= start;

      superpav_readbyte(sp,mp,lp, 0,1); /* reset reader */

      /* decode header to hd[]... */
      for(i=0; i<7; i++)
      {
         x= superpav_readbyte(sp,mp,lp, si, 0);     /* read a pav byte */
         byt= x & 0xFF;                             /* extract byte value. */
         nxt= (x & 0xFF00)>>8;                      /* and offset to next. */
         hd[i]= byt;
         si+=nxt;  /* bump si to point at offset to next byte. */
      }

      blk[row]->cs= (hd[2]+(hd[3]<<8) + hd[5]) & 0xFFFF;  /* get real load address. */
      blk[row]->cx= 256 -hd[5];                           /* get file size for this block. */
      blk[row]->ce= (blk[row]->cs + blk[row]->cx)-1;      /* compute end. */
      blk[row]->xi= (hd[4]*256) +(256 -hd[5]);            /* get total file size. */

      blk[row]->pilot_len= blk[row]->p2- blk[row]->p1;
      blk[row]->trail_len=0;

      /* SET THE STATIC LOAD ADDRESS FOR FOLLOWING SUB-BLOCKS (IF ANY).. */
      loadbase = blk[row]->cs + blk[row]->cx;

      sprintf(lin,"\n - Block Number : $%02X",hd[0]);
      strcat(info,lin);
      sprintf(lin,"\n - Sub-block number : $%02X",hd[1]);
      strcat(info,lin);
      sprintf(lin,"\n - Load address : $%04X",blk[row]->cs);
      strcat(info,lin);
      sprintf(lin,"\n - Total data size : %d bytes",blk[row]->xi);
      strcat(info,lin);
      sprintf(lin,"\n - Data in this block : %d bytes",blk[row]->cx);
      strcat(info,lin);
      sprintf(lin,"\n - Total sub-blocks in chain : %d",hd[4]);
      strcat(info,lin);
      
      tmp = ((hd[0]+hd[1]+hd[2]+hd[3]+hd[4]+hd[5]) & 0xFF)+6;

      if(hd[6]==tmp)
      {
         sprintf(lin,"\n - Header checkbyte : OK (expected=$%02X, actual=$%02X)",tmp,hd[6]);
         strcat(info,lin);
      }
      else
      {
         sprintf(lin,"\n - Header checkbyte : FAILED (expected=$%02X, actual=$%02X)",tmp,hd[6]);
         strcat(info,lin);
      }

      /* now decode the data part of this header...
         no reader reset required, just continue from above header read. */

      if(blk[row]->dd!=NULL)
         free(blk[row]->dd);
      blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

      rd_err=0;
      checkbyt=0;
      for(i=0; i<blk[row]->cx; i++)
      {
         x = superpav_readbyte(sp,mp,lp, si, 0);
         if(x==-1)
            rd_err++;
         byt = x & 0xFF;                /* extract byte */
         blk[row]->dd[i] = byt;
         nxt = (x & 0xFF00)>>8;         /* and offset to next. */
         si+=nxt;  /* bump si to point at offset to next byte. */
         checkbyt+=byt;
      }
      checkbyt+=(blk[row]->cx & 0xFF);  /* add number of bytes to checksum. */

      x= superpav_readbyte(sp,mp,lp, si, 0);     /* read checkbyte. */
      byt= x & 0xFF;                             /* extract byte value. */
      checkbyt&=0xFF;

      blk[row]->cs_exp= checkbyt;
      blk[row]->cs_act= byt;
      blk[row]->rd_err= rd_err;
      return 0;
   }

   /*----------------------------------------------------------------------------*/
   if(blk[row]->lt==SPAV1 || blk[row]->lt==SPAV2)  /* data sub-block?... */
   {
      start= blk[row]->p2;
      si= start;

      superpav_readbyte(sp,mp,lp, 0,1); /* reset reader */

      /* decode header to hd[]... */
      for(i=0; i<2; i++)
      {
         x= superpav_readbyte(sp,mp,lp, si,0);     /* read a pav byte */
         byt= x & 0xFF;                            /* extract byte value. */
         nxt= (x & 0xFF00)>>8;                     /* and offset to next. */
         hd[i]= byt;
         si+=nxt;  /* bump si to point at offset to next byte. */
      }

      /* compute C64 start address, end address and size... */
      blk[row]->cs= loadbase;
      blk[row]->ce= loadbase+255;
      blk[row]->cx= 256;

      blk[row]->pilot_len= blk[row]->p2- blk[row]->p1;
      blk[row]->trail_len=0;

      loadbase+=256;  /* NUDGE STATIC LOAD ADDRESS FOR NEXT SUB-BLOCK (IF ANY) */

      sprintf(lin,"\n - Block Number : $%02X",hd[0]);
      strcat(info,lin);
      sprintf(lin,"\n - Sub-block number : $%02X",hd[1]);
      strcat(info,lin);

      /* decode...
         no reader reset required, just continue from above header read. */
      if(blk[row]->dd!=NULL)
         free(blk[row]->dd);
      blk[row]->dd = (unsigned char*)malloc(blk[row]->cx);

      checkbyt=0;
      for(i=0; i<blk[row]->cx; i++)
      {
         x= superpav_readbyte(sp,mp,lp, si,0);     /* read a pav byte */
         byt= x & 0xFF;                            /* extract byte value. */
         blk[row]->dd[i] = byt;
         nxt= (x & 0xFF00)>>8;                     /* and offset to next. */
         si+=nxt;                      /* bump si to point at offset to next byte. */
         checkbyt+=byt;
      }

      checkbyt+= hd[0]+hd[1]+2;

      x= superpav_readbyte(sp,mp,lp, si,0);     /* read checkbyte. */
      byt= x & 0xFF;                            /* extract byte value. */

      checkbyt&=0xFF;
      blk[row]->cs_exp = checkbyt;
      blk[row]->cs_act = byt;
      blk[row]->rd_err = rd_err;
   }
   return 0;
}




