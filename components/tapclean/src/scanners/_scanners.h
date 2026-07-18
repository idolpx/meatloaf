/*
 * scanners.h
 *
 * Part of project "Final TAP".
 *
 * A Commodore 64 tape remastering and data extraction utility.
 *
 * (C) 2001-2006 Stewart Wilson, Subchrist Software.
 *
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
 * St, Fifth Floor, Boston, MA 02110-1301 USA
 */

void pause_search(void);
void pause_describe(int);

void cbm_search(void);
int cbm_describe(int);

void turbotape_search(void);
int turbotape_describe(int);

void turbotape_fast_search(void);
int turbotape_fast_describe(int);

void freeload_search(void);
int freeload_describe(int);

void odeload_search(void);
int odeload_describe(int);

void usgold_search(void);
int usgold_describe(int);

void nova_search(void);
int nova_describe(int);

void nova_spc_search(void);
int nova_spc_describe(int);

void aces_search(void);
int aces_describe(int);

void wild_search(void);
int wild_describe(int);

void megasave_search(int);	/* specify the Tx variant if discovered; 0 to try them all */
int megasave_describe(int);

void ocean_search(void);
int ocean_describe(int);

void raster_search(void);
int raster_describe(int);

void visiload_search(void);
int visiload_describe(int);

void cyberload_f1_search(void);
int cyberload_f1_describe(int);

void cyberload_f2_search(void);
int cyberload_f2_describe(int);

void cyberload_f3_search(void);
int cyberload_f3_describe(int);

void cyberload_f4_search(void);
int cyberload_f4_describe(int);

void bleep_search(void);
int bleep_describe(int);

void bleep_spc_search(void);
int bleep_spc_describe(int);

void hitload_search(void);
int hitload_describe(int);

void micro_search(void);
int micro_describe(int);

void burner_search(void);
int burner_describe(int);

void rackit_search(void);
int rackit_describe(int);

int superpav_readbyte(int, int, int, int, int);
void superpav_search(void);
int superpav_describe(int);

void anirog_search(void);
int anirog_describe(int);

int supertape_readbyte(int, int);
void supertape_search(void);
int supertape_describe(int);

int pav_readbyte(int, char);
void pav_search(void);
int pav_describe(int);

void ik_search(void);
int ik_describe(int);

void firebird_search(void);
int firebird_describe(int);

void turrican_search(void);
int turrican_describe(int);

void seuck1_search(void);
int seuck1_describe(int);

void jetload_search(void);
int jetload_describe(int);

void flashload_search(void);
int flashload_describe(int);

void virgin_search(void);
int virgin_describe(int);

void hitec_search(void);
int hitec_describe(int);

void tdi_search(void);
int tdi_describe(int);

void oceannew1_search(int);	/* specify the Tx variant if discovered; 0 to try them all */
int oceannew1_describe(int);

void atlantis_search(void);
int atlantis_describe(int row);

void snakeload51_search(void);
int snakeload51_describe(int);

void snakeload50_search(int);	/* specify the Tx variant if discovered; 0 to try them all */
int snakeload50_describe(int);

void palacef1_search(void);
int palacef1_describe(int);

void palacef2_search(void);
int palacef2_describe(int);

void oceannew2_search(void);
int oceannew2_describe(int);

void enigma_search(void);
int enigma_describe(int);

void audiogenic_search(void);
int audiogenic_describe(int);

void freezemachine_search(void);
int freezemachine_describe(int);

void aliensyndrome_search(void);
int aliensyndrome_describe(int);

void accolade_search(void);
int accolade_describe(int);

void alternativewg_search(void);
int alternativewg_describe(int);

void rainbowf1_search(void);
int rainbowf1_describe(int);

void rainbowf2_search(void);
int rainbowf2_describe(int);

void trilogic_search(void);
int trilogic_describe(int);

void burnervar_search(void);
int burnervar_describe(int);

void oceannew4_search(void);
int oceannew4_describe(int);

void tdif2_search(void);
int tdif2_describe(int);

void biturbo_search(void);
int biturbo_describe(int);

void t108DE0A5_search(void);
int t108DE0A5_describe(int);

void ar_search(void);
int ar_describe_hdr(int);
int ar_describe_data(int);

void ashdave_search(void);
int ashdave_describe(int);

void freeslow_search(int);	/* specify the Tx variant if discovered; 0 to try them all */
int freeslow_describe(int);

void goforgold_search(void);
int goforgold_describe(int);

void jiffyload_search(int);	/* specify the Tx variant if discovered; 0 to try them all */
int jiffyload_describe(int);

void fftape_search(void);
int fftape_describe(int);

void testape_search(void);
int testape_describe(int);

void tequila_search(void);
int tequila_describe(int);

void graphicadventurecreator_search(void);
int graphicadventurecreator_describe(int);

void chuckieegg_search(void);
int chuckieegg_describe(int);

void alternativedk_search(int);	/* specify the Tx variant if discovered; 0 to try them all */
int alternativedk_describe(int);

void powerload_search(void);
int powerload_describe(int);

void gremlin_f1_search(void);
int gremlin_f1_describe(int);

void gremlin_f2_search(void);
int gremlin_f2_describe(int);

void amaction_search(void);
int amaction_describe(int);

void creatures_search(void);
int creatures_describe(int);

void rainbowislands_search (void);
int rainbowislands_describe(int);

void oceannew3_search(void);
int oceannew3_describe(int);

void easytape_search(void);
int easytape_describe(int);

void turbo220_search(void);
int turbo220_describe(int);

void creativesparks_search(void);
int creativesparks_describe(int);

void ddesign_search(void);
int ddesign_describe(int);

void glass_search(void);
int glass_describe(int);

void turbotape526_search(void);
int turbotape526_describe(int);

void microloadvar_search(int);	/* specify the Tx variant if discovered; 0 to try them all */
int microloadvar_describe(int);

void lexpeed_search(void);
int lexpeed_describe(int);

void mms_search(void);
int mms_describe(int);

void gremlin_gbh_search(void);
int gremlin_gbh_describe(int);

void lk_avalon_search(void);
int lk_avalon_describe(int);

void turbotape263_search(void);
int turbotape263_describe(int);

void gyrospeed_search(void);
int gyrospeed_describe(int);

void msx_search(int);	/* specify the Tx variant if discovered; 0 to try them all */
int msx_describe(int);
