*=$c000  // Start address of the machine code program

LOOP:
  LDA $d012         // Load the current border color
  AND #%00001111    // Mask the color bits
  
  JSR $ffd2         // Call the Kernal routine to get a random number
  AND #%00001111    // Mask the lower 4 bits to get a random color
  
  ORA #$d020        // Combine with the upper bits of the color
  STA $d020         // Store the new border color
  
  JMP LOOP          // Repeat infinitely
  
*=$0801             // BASIC start address
!byte $0e,$08,$0a,$00,$9e,$20,$32,$30,$39,$36,$00

