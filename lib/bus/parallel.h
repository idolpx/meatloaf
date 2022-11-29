
#ifndef MEATLOAF_BUS_PARALLEL
#define MEATLOAF_BUS_PARALLEL

#include "gpiox.h"

/* User Port to pin mapping */
// #define FLAG2  P07  // B
// #define CNT1   P06  // 4
// #define SP1    P05  // 5
// #define CNT2   P04  // 6
// #define SP2    P03  // 7
// #define PC2    P02  // 8
// #define ATN    P01  // 9
// #define PA2    P00  // M

// #define PB0    P10   // C
// #define PB1    P11   // D
// #define PB2    P12   // E
// #define PB3    P13   // F
// #define PB4    P14   // H - G
// #define PB5    P15   // J - H
// #define PB6    P16   // K - I
// #define PB7    P17   // L - J

#define USERPORT_FLAGS GPIOX_PORT0
#define USERPORT_DATA  GPIOX_PORT1

typedef enum {
  FLAG2 = P07,  // B
  CNT1  = P06,  // 4
  SP1   = P05,  // 5
  CNT2  = P04,  // 6
  SP2   = P03,  // 7
  PC2   = P02,  // 8
  ATN   = P01,  // 9
  PA2   = P00,  // M - K

  PB0   = P10,  // C
  PB1   = P11,  // D
  PB2   = P12,  // E
  PB3   = P13,  // F
  PB4   = P14,  // H - G
  PB5   = P15,  // J - H
  PB6   = P16,  // K - I
  PB7   = P17,  // L - J
} user_port_pin_t;

typedef enum {
  MODE_SEND = 0,
  MODE_RECEIVE = 1
} parallel_mode_t;

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')

class parallelBus
{
  public:
    void setup();
    void reset();

    void handShake();
    uint8_t readByte();
    void writeByte( uint8_t byte );
    bool status( user_port_pin_t pin );

    uint8_t flags = 0;
    uint8_t data = 0;
    parallel_mode_t mode = MODE_RECEIVE;
    bool enabled = true;
};

void wic64_command();

extern parallelBus PARALLEL;

#endif // MEATLOAF_BUS_PARALLEL