//
// https://style64.org/petscii/
//

#include "qrcode-petscii.h"
#include <qrencode.h>
#include <cstring>

void QRcodePetscii::init() {
    screenwidth = sizeof(display) / 2;
    screenheight = screenwidth;
    memset(display, 0x20, sizeof(display));  // Clear

    int min = screenwidth;
    if (screenheight < screenwidth) min = screenheight;
    multiply = min / WD;
    offsetsX = (screenwidth - (WD * multiply)) / 2;
    offsetsY = (screenheight - (WD * multiply)) / 2;
}

void QRcodePetscii::screenwhite() {
    // CLR_HOME + BG_BLACK + CHR_WHITE
}

void QRcodePetscii::screenupdate() {
    // CLR_HOME + BG_BLACK + CHR_WHITE
}

void QRcodePetscii::drawPixel(int x, int y, int color) {
    int pos = (x * screenwidth) + y;
    display[pos] = color;
}

uint8_t QRcodePetscii::read_line(int x) {
    int q, q0, q1, q2, q3;
    int y = 0;
    int pos = (x * screenwidth) + y;

    for (int i = 0; i < screenwidth; i++)
        printf("%d", display[pos + i]);

    return 0x20;
}

uint8_t QRcodePetscii::read() {
    for (int x = 0; x < screenheight; x++) {
        read_line(x);
        printf("\r\n");
    }

    return 0x20;
}