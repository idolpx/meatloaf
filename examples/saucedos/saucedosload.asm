
.segmentdef main

.file [name="sdload.prg", segments="Default,main"]

.disk [filename="saucedos.d64", name="MEATLOAF SAUCEDOS", id="2023!"]
{
    [name="SDLOAD", type="prg", segments="Default,main"]
}

.segment main
*=$0801 "BASIC UPSTART"
 :BasicUpstart($0810)
*=$0810 "ACTUAL PROGRAM"

        lda #cmd_end-cmd
        ldx #<cmd
        ldy #>cmd
        jsr $FFBD     // call SETNAM

        lda #$0F      // file number 15
        ldx $BA       // last used device number
        bne _skip
        ldx #$08      // default to device 8
_skip:   
        ldy #$0F      // secondary address 15
        jsr $FFBA     // call SETLFS

        jsr $FFC0     // call OPEN
        bcs _error    // if carry set, the file could not be opened
_close:
        lda #$0F      // filenumber 15
        jsr $FFC3     // call CLOSE

        jsr $FFCC     // call CLRCHN
        rts
_error:
        // Akkumulator contains BASIC error code

        // most likely errors:
        // A = $05 (DEVICE NOT PRESENT)

        jmp _close    // even if OPEN failed, the file has to be closed

cmd:    .text "I"     // command string
cmd_end: