/*
 * loader_id.c
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
 *
 *
 * Notes:
 *
 * An effort to speed up the scanning process for TAPs, if I can ID the loader type after
 * the C64 ROM scan is done then I needn't search for ALL formats.
 *
 */


#include "main.h"
#include "mydefs.h"


/*
 * Looks through the crc database for a crc matching the paramater, returns the defined value
 * of the particular loader family if a match is found, else return 0.
 * The crc passed should obviously be that of the first CBM program in the TAP, whose length
 * must be passed through len.
 * If no match is found, then cbm_program[] and cbm_header[] are searched for known patterns.
 */

int idloader(unsigned /*long*/ int crc, int len)
{
	int i, id;

	static unsigned int kcrc[][2] = {
		{0x261DBA0E, LID_FREE},
		{0x8AAE883E, LID_FREE},
		{0x2EF78649, LID_FREE},
		{0x03969E4B, LID_FREE},
		{0xFCE154D8, LID_FREE},
		{0xF9D0BE97, LID_FREE},
		{0x5A1D548D, LID_FREE},
		{0xCF2A7534, LID_FREE},
		{0xA19943D0, LID_FREE},
		{0x95094A3A, LID_FREE},
		{0xF6EA5D74, LID_FREE},
		{0x6C280859, LID_FREE},
		{0xBBDC123F, LID_FREE},
		{0xB3EDBE12, LID_FREE},
		{0x15C61C29, LID_FREE},
		{0xDE2C7D61, LID_FREE},
		{0x4D7D872D, LID_FREE},
		{0x2CC1E59C, LID_FREE},
		{0x89D45404, LID_FREE},
		{0xE493F1F9, LID_FREE},		/* Orion, Ivan 'Ironman' Stewart's Super Off Road */

		{0xD5961DB0, LID_ODE},

		{0xAD318CC4, LID_BLEEP},
		{0x230936D6, LID_BLEEP},	/* Bubble Bobble, Rock'n Wrestle */
		{0x39B7A588, LID_BLEEP},	/* Code Hunter */
		{0x2277F4DD, LID_BLEEP},	/* Druid */
		{0x4CF069E0, LID_BLEEP},	/* Force One */
		{0x3B4A219E, LID_BLEEP},	/* Flying Shark, Graphic Editor, Gunstar, Twinky Goes Hiking, ICUPS, Mission A.D., Scorpius, UFO */
		{0xAFD39E15, LID_BLEEP},	/* Gothik, Halls of the Things, IO, Pandora, Rainbow Dragon */
		{0x9B898EF1, LID_BLEEP},	/* Harvey Headbanger */
		{0xAEAD5C1E, LID_BLEEP},	/* I, Ball */
		{0x8B5FA78A, LID_BLEEP},	/* Spiky Harold */
		{0x09A49BB4, LID_BLEEP},
		{0x58583B59, LID_BLEEP},	/* Park Patrol, Starglider, "Sentinel, The" */
		{0xF33ED7A2, LID_BLEEP},
		{0x4825AB54, LID_BLEEP},	/* Thrust */
		{0x5DD93BE5, LID_BLEEP},
		{0xEB752E5F, LID_BLEEP},	/* "Willow Pattern Adventure, The" */
		{0x1EB8DA0A, LID_BLEEP},	/* Samurai Warrior - The Battles Of Usagi Yojimbo */
		{0x2BF72881, LID_BLEEP},
		{0x59142D65, LID_BLEEP},	/* Star Trek - The Rebel Universe */
		{0x723AD943, LID_BLEEP},	/* Imagination */
		{0xE24E270C, LID_BLEEP},	/* Captain Fizz Meets The Blaster-Trons */
		{0xCD8D92EE, LID_BLEEP},	/* Decathlon */
		{0x9359531D, LID_BLEEP},	/* Ballistix */
		{0xA415CDE9, LID_BLEEP},	/* Batman - The Caped Crusader */
		{0x6D21D6E4, LID_BLEEP},	/* Batman - The Caped Crusader */
		{0x48BB8D93, LID_BLEEP},	/* Blood Money */
		{0x8D76173D, LID_BLEEP},	/* Cloud Kingdom */
		{0x35DEBB4F, LID_BLEEP},	/* Freak Factory */
		{0x2EF41AFB, LID_BLEEP},	/* Raging Beast */
		{0x001FE8ED, LID_BLEEP},	/* Seabase Delta */
		{0xC22F5F57, LID_BLEEP},	/* "Happiest Days Of Your Life, The" */
		{0xF858A7FA, LID_BLEEP},	/* Thunderbirds */
		{0x42D47C30, LID_BLEEP},	/* War Cars Construction Set */
		{0x05788B5F, LID_BLEEP},	/* Zolyx */
		{0xF8F0B452, LID_BLEEP},	/* American Road Race*/
		{0x83936DEA, LID_BLEEP},	/* Dynamic Duo */
		{0x7975553E, LID_BLEEP},	/* "Mystery Of The Nile, The" */
		{0x2989DE57, LID_BLEEP},	/* "OCP Art Studio, The" */
		{0xF206B18C, LID_BLEEP},	/* "OCP Art Studio, The" */
		{0x8B6D73BD, LID_BLEEP},	/* "Prince, The" */
		{0x226BA518, LID_BLEEP},	/* Mad Nurse */
		{0xBE6F5143, LID_BLEEP},	/* Chimera */
		{0x2ABE21AA, LID_BLEEP},	/* Slap&Tick */

		{0x366784C8, LID_MEGASAVE},	/* Formerly CHR */

		{0xD09BF46F, LID_BURN},
		{0x8D6E30E7, LID_BURN},
		{0x88613263, LID_BURN},
		{0x291E606A, LID_BURN},
		{0x6EA1C3AD, LID_BURN},

		{0xC618D67A, LID_WILD},
		{0x657CC9CD, LID_WILD},
		{0x554FAB46, LID_WILD},
		{0x8D14D322, LID_WILD},
		{0x7EBF1CC9, LID_WILD},
		{0xCDEB4E81, LID_WILD},
		{0x6B76D7C7, LID_WILD},
		{0xDC5FABC2, LID_WILD},
		{0xC949C625, LID_WILD},
		{0x97AB5438, LID_WILD},
		{0xFC8E7F96, LID_WILD},
		{0x69D7FCC6, LID_WILD},
		{0xA440C91A, LID_WILD},		/* Mindfighter */

		{0x00A517B5, LID_USG},
		{0x518F7B24, LID_USG},
		{0x3F6DD277, LID_USG},
		{0xD73E85F7, LID_USG},
		{0x548CC3DA, LID_USG},
		{0x834D852F, LID_USG},
		{0x6C791C31, LID_USG},
		{0x1BEBD222, LID_USG},
		{0x9DCF6B97, LID_USG},
		{0x81C86C4A, LID_USG},
		{0x7C1A7B45, LID_USG},
		{0x917CCAF0, LID_USG},
		{0x5FBA5BA7, LID_USG},
		{0x4C2A7C85, LID_USG},
		{0x70900492, LID_USG},
		{0x7BF88049, LID_USG},
		{0xDE86E455, LID_USG},
		{0xC6A33E82, LID_USG},
		{0xB50AB01A, LID_USG},
		{0x223890AF, LID_USG},
		{0xEA89FD1F, LID_USG},
		{0xB3AE738A, LID_USG},
		{0xE5869C31, LID_USG},
		{0xCBCCDB4E, LID_USG},
		{0xD0528E47, LID_USG},
		{0x25035DF8, LID_USG},
		{0x4FA62BEC, LID_USG},
		{0x266B6BD6, LID_USG},
		{0x17060B09, LID_USG},
		{0x32935096, LID_USG},		/* Buckaroo Banzai and Strange Odyssey */

		{0x0645F350, LID_MIC},
		{0x78C43CB9, LID_MIC},		/* Break Fever */
		{0xE0035766, LID_MIC},		/* Chameleon */
		{0x0EA016C6, LID_MIC},		/* Western Games, Canoeing, Vampire's Empire, Movie Monsters */
		{0x92571C3D, LID_MIC},		/* Dandy */
		{0x96677D19, LID_MIC},		/* Fungus */
		{0x9F20C2DB, LID_MIC},		/* Caves of 64 (aka Cave of 64), Fiends */
		{0xBC7F0E8D, LID_MIC},		/* Mission Elevator */
		{0xB1F027B4, LID_MIC},		/* Steve Davis Snooker */
		{0x178977C3, LID_MIC},
		{0x29DCB0D1, LID_MIC},		/* Elektrix */
		{0x91DC3707, LID_MIC},		/* Melon Mania */
		{0x9909A462, LID_MIC},		/* Desert Hawk */
		{0x54C129A6, LID_MIC},		/* Defender, China Miner, Aliens, Mermaid Madness */
		{0x8906017D, LID_MIC},		/* Empire */
		{0x5BC34E91, LID_MIC},		/* Brian Clough's Football Fortunes */
		{0xFD993947, LID_MIC},		/* Velocipede */
		{0x6F453F35, LID_MIC},		/* Bigtop Barney */
		{0xCCE0A5BD, LID_MIC},		/* Fruity */
		{0xB5392A8E, LID_MIC},		/* Auriga 64 */
		{0xB480091C, LID_MIC},		/* Clean up time */
		{0x44FB174A, LID_MIC},		/* Ronald Rubberduck */
		{0xE6314819, LID_MIC},		/* Velocipede II */
		{0xD994F40A, LID_MIC},		/* Guzzler */

		{0x58EEE22A, LID_ACE},		/* Ace of Aces */
		{0x915280DA, LID_ACE},
		{0x6A333F1D, LID_ACE},		/* Express Raider */
		{0x292409A7, LID_ACE},		/* Way of the Tiger */
		{0xF4CF55C8, LID_ACE},		/* Future Knight and Xevious */
		{0x4FC4C91C, LID_ACE},		/* Knight Games */
		{0xF1AB258B, LID_ACE},		/* Questprobe */
		{0xA3172F65, LID_ACE},		/* Leviathan */

		{0x60BCE3A3, LID_T250},
		{0x2D7372C2, LID_T250},
		{0xE3033FA9, LID_T250},
		{0x50C1FAFE, LID_T250},
		{0x1FDF834A, LID_T250},
		{0x8E399D97, LID_T250},
		{0x7D11900C, LID_T250},
		{0xC5C8015F, LID_T250},
		{0xB3A98C46, LID_T250},

		{0x67D4643C, LID_RACK},
		{0xAB3A1BAC, LID_RACK},
		{0x95518E9E, LID_RACK},
		{0x8E4CCA04, LID_RACK},

		{0x96285AA9, LID_OCEAN},
		{0x7E818F78, LID_OCEAN},
		{0x20CDF565, LID_OCEAN},
		{0xD2908C53, LID_OCEAN},
		{0xD3958D73, LID_OCEAN},
		{0x06EC1039, LID_OCEAN},
		{0xD70F4CBA, LID_OCEAN},
		{0xA9F2CE53, LID_OCEAN},
		{0x1C237E84, LID_OCEAN},
		{0xC1C4A2A0, LID_OCEAN},
		{0x985B5C4D, LID_OCEAN},
		{0xBD0ECB9E, LID_OCEAN},

		{0x04F2443B, LID_RAST},
		{0x880CA8A2, LID_RAST},

		{0xBDF9B4EF, LID_SPAV},
		{0x66CEE4E9, LID_SPAV},
		{0x603E8D53, LID_SPAV},
		{0x0412A711, LID_SPAV},
		{0x7CA20791, LID_SPAV},

		{0x5DC3AA69, LID_HIT},
		{0x3D9AD474, LID_HIT},

		{0x1482D2A7, LID_ANI},
		{0x0363E489, LID_ANI},
		{0x24BE37F6, LID_ANI},
		{0xA09E55E9, LID_ANI},

		{0x22734CE6, LID_ANI},		/* Anirog clone found in Polish tapes */

		{0xDD3DE175, LID_VIS_T1},
		{0x6B2A1236, LID_VIS_T1},	/* Atom Ant */
		{0xFC0DAD06, LID_VIS_T1},	/* Manor */
		{0x9EF77DD5, LID_VIS_T2},
		{0xF1340213, LID_VIS_T2},	/* BMX Simulator variant (first turbo file has 5 header bytes and 2 additional bits per byte) */
		{0x867C8969, LID_VIS_T3},
		{0xC7641A7E, LID_VIS_T4},
		{0xEB6DF918, LID_VIS_T4},	/* Exodus mastering parameters (first turbo file has 5 header bytes and 2 additional bits per byte) */
		{0x95FBE05E, LID_VIS_T4},	/* Subsunk mastering parameters (first turbo file uses LSbF endianness) */
		{0xB9F20338, LID_VIS_T4},	/* Chickin Chase, Headache, Sabre Wulf, The Helm mastering parameters (first turbo file has 5 header bytes, 2 additional bits per byte and uses LSbF endianness) */
		{0xBDFBF06B, LID_VIS_T4},	/* Estra mastering parameters (first turbo file 2 additional bits per byte and uses LSbF endianness) */
		{0xEF640A4B, LID_VIS_T4},	/* Circus Circus, Sabre Wulf, GoGo The Ghost mastering parameters (first turbo file has 2 additional bits per byte) */
		{0x91F2130D, LID_VIS_T4},	/* Booty mastering parameters (first turbo file has 5 header bytes and uses LSbF endianness) */
		{0xA39BB46C, LID_VIS_T4},	/* Fun School 2 (first turbo file has 2 additional bits per byte and uses LSbF endianness) */
		{0x2AA7A25A, LID_VIS_T5},	/* Critical Mass mastering parameters (first turbo file has 4 header bytes and 1 additional bit per byte) */
		{0xBBB52B89, LID_VIS_T5},	/* Spellseeker mastering parameters (same as Critical Mass) */
		{0xDE1AE55C, LID_VIS_T5},	/* Confuzion, Nick Faldo Plays The Open, TimeTrax, Mr. Mephisto mastering parameters (same as Critical Mass) */
		{0x7D657986, LID_VIS_T6},	/* Spindizzy 64 (part of Five Star Games) */
		{0xD6FE2E69, LID_VIS_T7},	/* Puffy's Saga, Borzak */

		{0xA7D33777, LID_FIRE},
		{0x041FDA59, LID_FIRE},

		{0x001B30E8, LID_NOVA},
		{0xA5950CFF, LID_NOVA},
		{0x5926FCC5, LID_NOVA},		/* Great Gurianos, Chain Reaction, Frank Bruno's Boxing */
		{0xF3D49D59, LID_NOVA},		/* Bomb Jack, Commando, Airwolf 2, Paperboy */

		{0x25197B4E, LID_IK},

		{0x5C34F43D, LID_PAV},

		{0x0936B7DF, LID_VIRG},
		{0x075AE096, LID_VIRG},		/* Fist 2 (Mastertronic) */
		{0x342A2416, LID_VIRG},		/* same as "Hi-Tec" loader but thres=$016E   Future Bike */
		{0x895DCF44, LID_VIRG},		/* This is "Virgin Loader"  (thres=$015E)   Guardian II, Alcazar, Toy Bizarre, Hard Drivin */

		{0xFADDF41C, LID_HTEC},		/* Chevy Chase, Top Cat, Yogi */

		{0x8E027BD2, LID_FLASH},
		{0x1754E006, LID_OCNEW1_T1},
		{0x3A35F804, LID_OCNEW1_T1},	/* Adidas Soccer */
		{0xC039C251, LID_OCNEW1_T2},
		{0x7FFB98B2, LID_OCNEW2},	/* Shadow Warriors */
		{0x9B132BD0, LID_OCNEW2},	/* Klax */
		{0xF96B0635, LID_OCNEW2},	/* Hero Quest */
		{0x908BEB44, LID_OCNEW2},	/* World Championship Boxing Manager */
		{0x7E4A9653, LID_OCNEW2},	/* Jahangir Khan World Championship Squash */

		{0x5622E174, LID_ATLAN},	/* Atlantis loader. */

		{0x206A8B68, LID_AUDIOGENIC},	/* Audiogenic loader */

		{0xF1D441D8, LID_FRZMACHINE},	/* Freeze Machine tape, often used by Cult titles */

		{0x9A447668, LID_ACCOLADE},	/* Accolade (Apollo 18, Miniputt) */
		{0xF22D0DAB, LID_ACCOLADE},	/* Motor Massacre, Test Drive, Duel, The: Test Drive II */
		{0xEA684D09, LID_ACCOLADE},	/* Accolade clone (EA: Chuck Yeager, PHP Pegasus, World Tour Golf) */

		{0xED3BB9E4, LID_RAINBOWARTS},	/* Circus Attractions, X-Out */
		{0xAC824E15, LID_RAINBOWARTS},	/* Grand Monster Slam (7 MB of TAP file!) */

		{0x53A23D4D, LID_BURNERVAR},	/* Hektik, Magic Carpet */
		{0x7E47C195, LID_BURNERVAR},	/* Mind Control, BMX Racers, Spectipede */

		{0xB211DFED, LID_OCNEW4},	/* Ocean New 4 */
		{0x4E56C97A, LID_OCNEW4},	/* Out Run Europa (Variant) */
		{0x6B4A0009, LID_OCNEW4},	/* Liverpool (Variant) */

		{0x108DE0A5, LID_108DE0A5},	/* REC magazine tapes (popular in Italy) */

		{0xAE9A2682, LID_FREE_SLOW},	/* Freeload Slowload (Platoon) */
		{0x0AC328F5, LID_FREE_SLOW},	/* Freeload Slowload (Rambo, Great Escape, Top Gun) */
		{0xB5694463, LID_FREE_SLOW},	/* Freeload Slowload T2 (Smith's Super Champs) */

		{0x565C0FCD, LID_GOFORGOLD},	/* Go For The Gold (Peepo's) */
		{0xE546B793, LID_GOFORGOLD},	/* Go For The Gold (Kevin's) */

		{0x958D7E97, LID_JIFFYLOAD},	/* T1: The Evil Dead (Peepo's & D-ram's) */
		{0x34AAE466, LID_JIFFYLOAD},	/* T1: Special Operations */
		{0xA86F2C56, LID_JIFFYLOAD},	/* T2: Robin to The Rescue, Munch Man */
		{0x0B302531, LID_JIFFYLOAD},	/* T2: Jungle Quest */
		{0x94EAA6AF, LID_JIFFYLOAD},	/* T2: Jungle Quest */
		{0x7F7653E9, LID_JIFFYLOAD},	/* T2: Monkey Magic */

		{0xE356E438, LID_FFTAPE},	/* Freeze Frame (whose CBM filename is "FF") */

		{0xDB93D097, LID_TESTAPE},	/* Special Program games */

		{0x737CE3A9, LID_TEQUILA},	/* Tequila Sunrise */

		{0x823DBD1F, LID_GRADVCREATOR},	/* Graphic Adv Creator tape (was Alternative Software) */

		{0xD7483CDA, LID_CHUCKIEEGG},	/* Chuckie Egg */

		{0x057A87A2, LID_ALTERDK},	/* Early Alternative Software loader */
		{0xDA74865F, LID_ALTERDK},	/* Arcade Classics (T3) */
		{0x4A46B891, LID_ALTERDK},	/* Special Agent (T3) */
		{0x1D3C3608, LID_ALTERDK},	/* Interdictor Pilot (T4) */

		{0xD407EAA3, LID_POWERLOAD},	/* Power Load: Felix in the factory, Ghouls, Jet Power Jack, Stock Car */
		{0xA217E2B1, LID_POWERLOAD},	/* Mr Wiz, Smuggler, U.K. Geography, World Geography */
		{0xCD358874, LID_POWERLOAD},	/* Percy Penguin */
		{0xE553098E, LID_POWERLOAD},	/* Mini Office, World Cup II */
		{0xC95F137D, LID_POWERLOAD},	/* World Cup II (other source) */

		{0x71345F31, LID_POWERLOAD},	/* Power Load variant: Bombo */
		{0x84A332B3, LID_POWERLOAD},	/* Ian Botham's Test Match, Euro Games 64 */
		{0x8C61AFE2, LID_POWERLOAD},	/* Jack Charltons Fishing */
		{0x9D0FC1AE, LID_POWERLOAD},	/* Lightforce */

		{0xAD9ABE1C, LID_GREMLIN},	/* Gremlin tape F1 and F2 */
		{0xC721D847, LID_GREMLIN},

		{0x40DF4E6A, LID_EASYTAPE},	/* Easy-Tape System C */
		{0x0B8B7156, LID_EASYTAPE},
		{0x691067FA, LID_EASYTAPE},

		{0xD4ACE22C, LID_CSPARKS},	/* Creative Sparks: Chopper */
		{0x35AAD887, LID_CSPARKS},	/* Danger Mouse in Double Trouble */
		{0x63CDFCDA, LID_CSPARKS},	/* Mad Doctor */
		{0x23EE788B, LID_CSPARKS},	/* Taskmaster */
		{0xD13ACEED, LID_CSPARKS},	/* Kayak */
		{0xE14AA64D, LID_CSPARKS},	/* Java Jim in Square Shaped Trouble */

		/* Disabled: Unfortunately the data section is just a pointer to $0351, which is common to other loaders too */
		/*{0x848FD0AF, LID_TRILOGIC},*/	/* Additional releases: Double Dare,
						   Fireman Sam, Hellfire Attack,
						   Kentucky Racing, Merlin, Metranaut,
						   Strike Force, Superted */

		{0x114EEF5B, LID_GLASS},	/* Pyramid, The */
		{0x606B287D, LID_GLASS},	/* Cliff Hanger */

		{0xEBC00CA0, LID_MICVAR},	/* Steve Davis Snooker */
		{0x90BA334E, LID_MICVAR},	/* MahJong */
		{0x65E7E156, LID_MICVAR},	/* Pinball */
		{0xE5CACA00, LID_MICVAR},	/* Video Card */
		{0x2E8ADA25, LID_MICVAR},	/* Wulfpack, Hi-Q-Quiz */
		{0x69992CDE, LID_MICVAR},	/* Ice Hockey (aka International Hockey) */
		{0xA0AD37D3, LID_MICVAR},	/* Syntax, Ice Temple */
		{0xC998B9EA, LID_MICVAR},	/* On The Bench (by Cult) */

		{0x3C06E1A2, LID_LEXPEED},	/* De Sekte... */
		{0x4B83F346, LID_LEXPEED},	/* Big Deal */
		{0x13BB9826, LID_LEXPEED},	/* Nacht Wacht */
		{0xAB5E53E9, LID_LEXPEED},	/* Het Spel En De Knikkers */
		{0x44EB4695, LID_LEXPEED},	/* All Risks */
		{0x8EEAE96E, LID_LEXPEED},	/* Endless, Floyd The Droid, Verkeersrally */
		{0x197FBC99, LID_LEXPEED},	/* Hollanditis */
		{0x75115ADC, LID_LEXPEED},	/* Radeloos */
		{0x3C06E1A2, LID_LEXPEED},	/* Hopeless */
		{0xB4E6A165, LID_LEXPEED},	/* Chip Nibbel, Dr.J */
		{0x89B7C224, LID_LEXPEED},	/* Champ, Sprite Machine */

		{0x14FA292E, LID_MMS},		/* Drip */
		{0x1127DC9A, LID_MMS},		/* Dr. Mad */
		{0x06500A4A, LID_MMS},		/* Castle */

		{0xAFA566C3, LID_GREMLINGBH},	/* Impossamole */
		{0x1F019DE4, LID_GREMLINGBH},	/* Lotus Esprit Turbo Challenge */

		{0xF47019A4, LID_GYROSPEED},	/* BASIC program */
		{0xDF90D81A, LID_GYROSPEED},	/* Machine code program */

		{0, 0}				/* List terminator/cap */
	};


	/* search crc table for alias... */

	id = 0;

	for (i = 0; kcrc[i][0] != 0; i++) {
		if (crc == kcrc[i][0]) {
			id = kcrc[i][1];
			break;
		}
	}

	#define MAXBLOCKLOOKAHEAD 4 /* Max displacement of the array element that is read below */

	/* crc search failed?... do a search in 'cbm_program[]' for loader ID strings... */

	if (id == 0) {

		for (i = 0; i < len - MAXBLOCKLOOKAHEAD; i++) {

			/* look for N... OVA */

			if (cbm_program[i] == 0x0E &&
					cbm_program[i + 1] == 0x0F &&
					cbm_program[i + 2] == 0x16 &&
					cbm_program[i + 3] == 0x01) {

				id = LID_NOVA;

				break;
			}

			/* look for N... OVA   (as ASCII) ie. Breakthru */

			if (cbm_program[i] == 0x4E &&
					cbm_program[i + 1] == 0x4F &&
					cbm_program[i + 2] == 0x56 &&
					cbm_program[i + 3] == 0x41) {

				id = LID_NOVA;

				break;
			}

			/* and look for C... YBER  (Cyberload) */

			if (cbm_program[i] == 0x03 &&
					cbm_program[i + 1] == 0x19 &&
					cbm_program[i + 2] == 0x02 &&
					cbm_program[i + 3] == 0x05 &&
					cbm_program[i + 4] == 0x12) {

				id = LID_CYBER;

				break;
			}

			/* and look for G... O AWAY  (Ocean) */

			if (cbm_program[i] == 0x47 &&
					cbm_program[i + 1] == 0x4F &&
					cbm_program[i + 2] == 0x20 &&
					cbm_program[i + 3] == 0x41 &&
					cbm_program[i + 4] == 0x57) {

				id = LID_OCEAN;

				break;
			}

			/* look for S... NAKE (Snakeload 5.0 or 5.1) */

			if (cbm_program[i] == 0x53 &&
					cbm_program[i + 1] == 0x4E &&
					cbm_program[i + 2] == 0x41 &&
					cbm_program[i + 3] == 0x4B &&
					cbm_program[i + 4] == 0x45) {

				id = LID_SNAKE;

				break;
			}
		}
	}

	/* crc search failed?... do a search in 'cbm_header[]' for loader ID strings... */

	if (id == 0) {

		for (i = 0; i < 192 - MAXBLOCKLOOKAHEAD; i++) {

			/* look for L... EXPE (Lexpeed by Lex Boere - Radarsoft) */

			if (cbm_header[i] == 0xCC &&
					cbm_header[i + 1] == 0xC5 &&
					cbm_header[i + 2] == 0xD8 &&
					cbm_header[i + 3] == 0xD0 &&
					cbm_header[i + 4] == 0xC5) {

				id = LID_LEXPEED;

				break;
			}
		}
	}

	return id;
}
