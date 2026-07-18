/*---------------------------------------------------------------------------
  supertape.c

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

  - read errors in supertape are fatal as we need a good read of each byte to
    find the proper location of the next!.
     
  - rd_err is not being counted during decode.

---------------------------------------------------------------------------*/

#include "../mydefs.h"
#include "../main.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define HDSZ 27

/*---------------------------------------------------------------------------*/
#define ST_READ        0
#define ST_SETSTAT1    1
#define ST_SETSTAT2    2
#define ST_CLEAR       3

static int _dst=0;  /* set when a header file is found... */
static int _dsz=0;  /* used and cleared when data file is found. */
static int _den=0;

/*---------------------------------------------------------------------------
 call with mode==0 to read a byte.
 with mode==3 to clear the bit buffer
 with mode==1 or mode==2 to over-ride current status AND reset.
 see #defines (above).
 Returns -1 on error.
---------------------------------------------------------------------------*/
int supertape_readbyte(int pos, int mode)
{
   /* status 1 lookup table... */
   int tab1[3][2] = {{1, 0},    /* S  "0" */
                     {2, 01},   /* M  "10" (reversed for LSBF) switch to other */
                     {2, 3}};   /* L  "11"  force status 1  (resetm) */

   /* status 2 lookup table... */
   int tab2[3][2] = {{1, 0},    /* S  "0" */
                     {1, 1},    /* M  "1"   switch to other */
                     {2, 3}};   /* L  "11"  force status 1  (resetm) */

   int p,pcnt,a,bits,val,sw,byt, resetm=0;
   static int status=1, bpos=0, bbuf=0;

   if(mode==ST_CLEAR)   /* user just wants to reset... (status is unchanged) */
   {
      bpos=0;    /* (just clear previous buffer contents...) */
      bbuf=0;
      return 0;
   }
   if(mode==ST_SETSTAT1 || mode==ST_SETSTAT2)  /* user wants to set the status AND reset... */
   {
      status=mode;
      bpos=0;
      bbuf=0;
      return 0;
   }
   if(mode==ST_READ)
   {
      /*
       * Note by Luigi: we should not stop at len-8 as certain pulses migh
       * contribute more than one bit based on the state of the reader
       */
      if(pos>(tap.len-4) || pos<20) /* return -1 if out of bounds. */
         return -1;
      if(is_pause_param(pos))
         return -1;

      pcnt=0;  /* counts the number of pulses used for this byte. */

      do
      {
         p = tap.tmem[pos];

         a=-1;  /* we can test read errors by presetting this to error code. */

         if(p>(ft[SUPERTAPE_HEAD].sp-tol) && p<(ft[SUPERTAPE_HEAD].sp+tol))  /* SHORT. */
         { a=0; sw=0; resetm=0;}
         if(p>(ft[SUPERTAPE_HEAD].mp-tol) && p<(ft[SUPERTAPE_HEAD].mp+tol))  /* MEDIUM. (switch status) */
         { a=1; sw=1; resetm=0;}
         if(p>(ft[SUPERTAPE_HEAD].lp-tol) && p<(ft[SUPERTAPE_HEAD].lp+tol))  /* LONG. (force status 1) */
         { a=2; sw=0; resetm=1; }

         /*if(status==2 && a==2)
          Application->MessageBox("FOUND 2:2", "Oops", NULL); */
         
         if(a==-1)  /* read error, pulsewidth out of range */
         {
            add_read_error(p);
            return -1;
         }

         if(status==1)
         {
            bits=tab1[a][0];
            val =tab1[a][1];
            if(sw)
               status=2;
         }
         else /* status==2. */
         {
            bits=tab2[a][0];
            val =tab2[a][1];
            if(sw)
               status=1;
         }

         if(resetm)
         {
            status=1;
            resetm=0;
         }

         bbuf = bbuf | (val<<bpos);
         bpos+=bits;   /* bump bit counter. */
         pos++;        /* bump file offset. */
         pcnt++;

      }
      while(bpos<8 && pos<tap.len);  /* loop until we have (at least) 8 bits or reach the end of tape. */

      if(a==-1 || bpos<8)
         return -1;  /* read error or not enough bits read. */


      byt = bbuf & 0xFF;

      bbuf = bbuf>>8;  /* slide off lower 8 bits of bbuf. */
      bpos-=8;         /* and reduce bit position by 8. */

      return(byt+(pcnt<<8));  /* return 8 bits of bbuf */
                              /* +number of pulses used. */

   }
   return 0;
}
/*---------------------------------------------------------------------------
*/
void supertape_search(void)
{
   int i,sof,sod,eod,eof;
   int j,x,b,s,byt,nxt,hd[HDSZ], pilots;
   
   if(!quiet)
      msgout("  Supertape");
         

   for(i=20; i<tap.len-100; i++)
   {
      supertape_readbyte(0,ST_SETSTAT1);
      x= supertape_readbyte(i, ST_READ);
      byt= x & 0xFF;

      if(byt==ft[SUPERTAPE_HEAD].pv)
      {
         sof=i;
         pilots=0;
         do
         {
            pilots++;

            x= supertape_readbyte(i, ST_READ);
            byt= x & 0xFF;
            nxt= (x & 0xFF00)>>8;
            i+=nxt;

         }
         while(byt==ft[SUPERTAPE_HEAD].pv);  /* scan til end of pilot... */

         if(pilots>9)
         {
            byt&=0xEF;

            if(byt==ft[SUPERTAPE_HEAD].sv)   /* we found a header block... */
            {
                sod=i;

                /* have to read through all header bytes to find end offsets */
                for(j=0; j<HDSZ; j++)
                {
                   if(j==25)
                      eod=i;  /* record eod (2 bytes before end) */
                   x = supertape_readbyte(i, ST_READ);
                   nxt = (x & 0xFF00)>>8;
                   i+=nxt;
                }
                eof=i;
                addblockdef(SUPERTAPE_HEAD, sof,sod,eod,eof, 0);

                /*.....................................................
                  decode header and pull out infos for PROG block... */

            /* i have to get loader into correct simulated state (status AND buffer)
               before i can correctly decode...
               to do this the whole pilot and sync byte must be read through...

               For some reason it isn't in the correct state at this point and
               reading the header values without this next part causes them to
               appear twice the size they should be! (bug fixed 16-06-05) */

                s= sof;   /* read through pilot and sync sequence... */
                supertape_readbyte(0, ST_SETSTAT1);
                do
                {
                   x= supertape_readbyte(s, ST_READ);
                   s+=(x & 0xFF00)>>8;
                }
                while(s<sod);


                s= sod;
                for(j=0; j<HDSZ; j++)
                {
                   b= supertape_readbyte(s, ST_READ);
                   hd[j]= (b & 0xFF);
                   s+=(b & 0xFF00)>>8;
                }
                _dsz= hd[19]+(hd[20]<<8);  /* store size globally */
                /*.....................................................*/
            }

            if(byt==ft[SUPERTAPE_DATA].sv)  /* we found a data block... */
            {
                sod= i;

                /* read ALL bytes (to _dsz) to find end... */
                s= sod;
                for(j=0; j<_dsz +2; j++)   /* +2 includes 2 checksum bytes */
                {
                   if(j==_dsz)  /* record eod (2 bytes before end) */
                      eod= s;
                   b= supertape_readbyte(s, ST_READ);
                   s+=(b & 0xFF00)>>8;
                }

                eof= s;
                addblockdef(SUPERTAPE_DATA, sof,sod,eod,eof, 0);
                _dsz=0;  /* reset this so no other data block can use previous headers info. */
            }
         }
      }
   }
}
/*---------------------------------------------------------------------------
*/
int supertape_describe(int row)
{
   int i,s,j,b,byt,tmp,chk,checksum,b1,b2,hd[25];
   char fn[256],str[2000];

   /* i have to get loader into correct simulated state (status AND buffer)
      before i can correctly decode...
      to do this the whole pilot and sync byte must be read through... */

   s= blk[row]->p1;   /* read through pilot and sync sequence... */
   supertape_readbyte(0, ST_SETSTAT1);
   do
   {
      b= supertape_readbyte(s, ST_READ);
      s+=(b & 0xFF00)>>8;
   }
   while(s < blk[row]->p2);

   /*--------------------------------------------------------------------------*/
   if(blk[row]->lt == SUPERTAPE_HEAD)
   {
      s= blk[row]->p2;

      /* compute C64 start address, end address and size... */
      blk[row]->cs= 0x033C;
      blk[row]->cx= HDSZ-2;  /* -2 omits the 2 parity checkbytes */
      blk[row]->ce= blk[row]->cs + blk[row]->cx -1;

      /* decode header... */
      for(i=0; i<blk[row]->cx; i++)
      {
         b= supertape_readbyte(s, ST_READ);
         byt= (b & 0xFF); /* & 0xEF; */
         hd[i]= byt;
         s+=(b & 0xFF00)>>8;
      }

      /* extract file name from block... */
      for(i=0; i<16; i++)
         fn[i]= hd[i];
      fn[i]=0;
      trim_string(fn);
      pet2text(str,fn);

      if(blk[row]->fn!=NULL)
         free(blk[row]->fn);
      blk[row]->fn = (char*)malloc(strlen(str)+1);

      strcpy(blk[row]->fn, str);

      /* save start address and size for any data block being described next... */
      _dst= hd[17] + (hd[18]<<8);
      _dsz= hd[19] + (hd[20]<<8);
      _den= _dst + _dsz;

      sprintf(lin,"\n - DATA Load address : $%04X", _dst);
      strcat(info,lin);
      sprintf(lin,"\n - DATA File size : %d bytes", _dsz);
      strcat(info,lin);
      sprintf(lin,"\n - DATA End address (calculated) : $%04X", _den);
      strcat(info,lin);
   }

   /*--------------------------------------------------------------------------*/
   if(blk[row]->lt == SUPERTAPE_DATA)
   {
      /* compute C64 start address, end address and size... */
      blk[row]->cs= _dst;   /* (use values globally stored by previous header.) */
      blk[row]->ce= _den;
      blk[row]->cx= _dsz;
   }

   /* common code for both Headers and Data blocks... */

   /* get pilot length... */
   s= blk[row]->p1;
   j= 0;
   supertape_readbyte(0, ST_SETSTAT1);
   do
   {
      b= supertape_readbyte(s, ST_READ);
      byt= (b & 0xFF);
      s+=(b & 0xFF00)>>8;
      j++;
   }
   while(byt==0x16);
   blk[row]->pilot_len= j-1;
   blk[row]->trail_len= 0;


   /* extract data and test checksum... */
   chk= 0;

   if(blk[row]->dd!=NULL)
      free(blk[row]->dd);
   blk[row]->dd= (unsigned char*)malloc(blk[row]->cx);

   for(i=0; i<blk[row]->cx; i++)
   {
      b= supertape_readbyte(s, ST_READ);
      byt= (b & 0xFF);
      if(byt==-1)
         blk[row]->rd_err++;
      blk[row]->dd[i]= byt;
      s+=(b & 0xFF00)>>8;

      /* count the number of 1 bits in last byte... (develops parity checksum) */
      tmp= byt;
      for(j=0; j<8; j++)
      {
         tmp<<=1;
         if(tmp&0x100)
            chk++;
      }
   }

   /* read actual parity checksum (2 bytes)... */
   b= supertape_readbyte(s, ST_READ);
   b1= b & 0xFF;
   s+=(b & 0xFF00)>>8;
   b= supertape_readbyte(s, ST_READ);
   b2= b & 0xFF;
   /*s+=(b & 0xFF00)>>8; (un-needed) */
   checksum= b1+(b2<<8);

   blk[row]->cs_exp= chk & 0xFFFF;
   blk[row]->cs_act= checksum;
   return 0;
}

