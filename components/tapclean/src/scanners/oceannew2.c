/*---------------------------------------------------------------------------
  oceannew2.c (Ocean New Format 2)

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
#include <stdio.h>
#include <stdlib.h>

#define HDSZ 5

/*---------------------------------------------------------------------------
*/
void oceannew2_search(void)
{
	int i,sof,sod,eod,eof;
	int z,h,hd[HDSZ];
	unsigned int s,e,x;

	if(!quiet)
		msgout("  New Ocean Tape 2");
				 
	 
	for(i=20; i<tap.len-8; i++) {

		if((z=find_pilot(i,OCNEW2))>0) {
			 sof=i;
			 i=z;

			 if(readttbyte(i, ft[OCNEW2].lp, ft[OCNEW2].sp, ft[OCNEW2].tp, ft[OCNEW2].en)==ft[OCNEW2].sv) {
				sod=i+8;
				/* decode the header so we can validate the addresses... */
				for(h=0; h<HDSZ; h++) {
					hd[h] = readttbyte(sod+(h*8), ft[OCNEW2].lp, ft[OCNEW2].sp, ft[OCNEW2].tp, ft[OCNEW2].en);
					if (hd[h] == -1)
						break;
				}

				if (h != HDSZ)
					continue;

				s=hd[0]+(hd[1]<<8);	 /* get start address */
				e=hd[2]+(hd[3]<<8);	 /* get end address */

				if (e>s) {
					x=e-s;
					eod=sod+((x+HDSZ)*8);
					eof=eod+7;
					addblockdef(OCNEW2, sof,sod,eod,eof, 0);
					i=eof;	/* optimize search */
				}
			}
		} else {
			if(z<0)		/* find_pilot failed (too few/many), set i to failure point. */
				i=(-z);
		}
	 }
}

/*---------------------------------------------------------------------------
*/
int oceannew2_describe(int row)
{
	int i,s,b,cb,hd[HDSZ];
	 
	/* decode the header to get load address etc... */
	s= blk[row]->p2;
	for(i=0; i<HDSZ; i++)
		hd[i] = readttbyte(s+(i*8), ft[OCNEW2].lp, ft[OCNEW2].sp, ft[OCNEW2].tp, ft[OCNEW2].en);

	blk[row]->cs= hd[0]+(hd[1]<<8);
	blk[row]->ce= hd[2]+(hd[3]<<8)-1;
	blk[row]->cx= (blk[row]->ce - blk[row]->cs)+1;	

	sprintf(lin,"\n - ID Byte : $%02X",hd[4]);	 /* show ID byte */
	strcat(info,lin);

	/* get pilot & trailer length */
	blk[row]->pilot_len= (blk[row]->p2- blk[row]->p1 -8)>>3;
	blk[row]->trail_len=0;

	/* extract data and test checksum... */
	cb= 0;
	s= blk[row]->p2+(HDSZ*8);

	if(blk[row]->dd!=NULL)
		free(blk[row]->dd);
	blk[row]->dd= (unsigned char*)malloc(blk[row]->cx);

	for(i=0; i<blk[row]->cx; i++) {
		b= readttbyte(s+(i*8), ft[OCNEW2].lp, ft[OCNEW2].sp, ft[OCNEW2].tp, ft[OCNEW2].en);
		cb^= b;
		if(b==-1)
			blk[row]->rd_err++;
		blk[row]->dd[i]= b;
	}
	b= readttbyte(s+(i*8), ft[OCNEW2].lp, ft[OCNEW2].sp, ft[OCNEW2].tp, ft[OCNEW2].en); /* read actual checkbyte. */

	blk[row]->cs_exp= cb &0xFF;
	blk[row]->cs_act= b;

	return 0;
}

