#ifndef QRCODE_PETSCII_H
#define QRCODE_PETSCII_H

#include <qrcodedisplay.h>

class QRcodePetscii : public QRcodeDisplay {
   private:
    uint8_t display[50 * 50];
    void drawPixel(int x, int y, int color);

   public:
    void init() override;
    void screenwhite() override;
    void screenupdate() override;
    uint8_t read_line(int x);
    uint8_t read();
};

#endif  // QRCODE_PETSCII_H