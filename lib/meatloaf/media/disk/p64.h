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