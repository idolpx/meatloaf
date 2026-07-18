// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

//
// https://github.com/markusC64/p64conv/blob/master/lib/p64refimp/p64tech.txt
// https://www.cbmstuff.com/forum/showthread.php?tid=70&pid=302
//

// Bit 1 of the flags is read only as written down in the VICE page. But there
// are other flags not mentioned there:
// * Bit 2 is high resolution, if set, the timing values aren't 16 MHz but 48
// MHz.

// You might wonder why 48 MHz? Well, the 16 MHz are from the internal hardware
// of the 1541. The CBM 8250 has 24 MHz instead. And the least common multiple is
// - yes, 48 MHz.

// If the floppy is the P64 file format is set to "8250", an implementation that
// handles multiple different CBM floppies should therefore assume 24 MHz resp.
// 48 MHz. It's extension would be P82. Or D82 for the sector dump.

// P81 obviously has only the double sided case :-)