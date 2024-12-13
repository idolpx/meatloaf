// #ifdef BUILD_IEC
// #ifdef JIFFYDOS
// // Meatloaf - A Commodore 64/128 multi-device emulator
// // https://github.com/idolpx/meatloaf
// // Copyright(C) 2020 James Johnston
// //
// // Meatloaf is free software : you can redistribute it and/or modify
// // it under the terms of the GNU General Public License as published by
// // the Free Software Foundation, either version 3 of the License, or
// // (at your option) any later version.
// //
// // Meatloaf is distributed in the hope that it will be useful,
// // but WITHOUT ANY WARRANTY; without even the implied warranty of
// // MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// // GNU General Public License for more details.
// //
// // You should have received a copy of the GNU General Public License
// // along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

// #include "jiffydos.h"

// #include <rom/ets_sys.h>
// // #include <freertos/FreeRTOS.h>
// // #include <freertos/task.h>
// // #include <freertos/semphr.h>
// // #include <driver/timer.h>

// #include "../../../include/debug.h"
// #include "../../../include/pinmap.h"
// #include "_protocol.h"
// #include "bus.h"

// using namespace Protocol;

// JiffyDOS::JiffyDOS() {
//     // Fast Loader Pair Timing
//     bit_pair_timing.clear();
//     bit_pair_timing = {
//         {14, 27, 38, 51},  // Receive
//         {17, 27, 39, 50}   // Send
//     };
// };

// uint8_t IRAM_ATTR JiffyDOS::receiveByte() {
//     // IEC_ASSERT(PIN_DEBUG);
//     uint8_t data = 0;

//     portDISABLE_INTERRUPTS();

//     IEC.flags &= CLEAR_LOW;

//     // Release data to signal we are ready
//     IEC_RELEASE(PIN_IEC_DATA_OUT);

//     // Wait for talker ready
//     while (IEC_IS_ASSERTED(PIN_IEC_CLK_IN)) {
//         if (IEC_IS_ASSERTED(PIN_IEC_ATN)) {
//             IEC.flags |= ATN_ASSERTED;
//             goto done;
//         }
//     }
//     // Wait for talker to be ready
//     // if (waitForSignals(PIN_IEC_CLK_IN, IEC_RELEASED, PIN_IEC_ATN, IEC_ASSERTED,
//     //                    TIMEOUT_DEFAULT) == TIMED_OUT) {
//     //     IEC.flags |= ATN_ASSERTED;
//     //     goto done;
//     // }

//     // RECEIVING THE BITS
//     // As soon as the talker releases the Clock line we are expected to receive
//     // the bits Bits are inverted so use IEC_IS_ASSERTED() to get
//     // asserted/released status

//     IEC_ASSERT(PIN_DEBUG);
//     timer_start();

//     // get bits 4,5
//     timer_wait_until(14);
//     if (IEC_IS_ASSERTED(PIN_IEC_CLK_IN)) data |= 0b00010000;   // 0
//     if (IEC_IS_ASSERTED(PIN_IEC_DATA_IN)) data |= 0b00100000;  // 1
//     IEC_RELEASE(PIN_DEBUG);

//     // get bits 6,7
//     timer_wait_until(27);
//     if (IEC_IS_ASSERTED(PIN_IEC_CLK_IN)) data |= 0b01000000;   // 0
//     if (IEC_IS_ASSERTED(PIN_IEC_DATA_IN)) data |= 0b10000000;  // 0
//     IEC_ASSERT(PIN_DEBUG);

//     // get bits 3,1
//     timer_wait_until(38);
//     if (IEC_IS_ASSERTED(PIN_IEC_CLK_IN)) data |= 0b00001000;   // 0
//     if (IEC_IS_ASSERTED(PIN_IEC_DATA_IN)) data |= 0b00000010;  // 0
//     IEC_RELEASE(PIN_DEBUG);

//     // get bits 2,0
//     timer_wait_until(51);
//     if (IEC_IS_ASSERTED(PIN_IEC_CLK_IN)) data |= 0b00000100;   // 1
//     if (IEC_IS_ASSERTED(PIN_IEC_DATA_IN)) data |= 0b00000001;  // 0
//     IEC_ASSERT(PIN_DEBUG);

//     // Check CLK for EOI
//     timer_wait_until(64);
//     if (IEC_IS_ASSERTED(PIN_IEC_CLK_IN)) IEC.flags |= EOI_RECVD;
//     IEC_RELEASE(PIN_DEBUG);

//     // Acknowledge byte received
//     // If we want to indicate an error we can release DATA
//     IEC_ASSERT(PIN_IEC_DATA_OUT);

//     // Wait for sender to read acknowledgement
//     timer_wait_until(83);

//     // Debug_printv("data[%02X] eoi[%d]", data, IEC.flags); // $ = 0x24

// done:
//     IEC_ASSERT(PIN_DEBUG);
//     portENABLE_INTERRUPTS();

//     IEC_RELEASE(PIN_DEBUG);
//     return data;
// }  // receiveByte

// // STEP 1: READY TO SEND
// // Sooner or later, the talker will want to talk, and send a character.
// // When it's ready to go, it releases the Clock line to false.  This signal
// // change might be translated as "I'm ready to send a character." The listener
// // must detect this and respond, but it doesn't have to do so immediately. The
// // listener will respond  to  the  talker's "ready  to  send"  signal  whenever
// // it  likes;  it  can  wait  a  long  time.    If  it's a printer chugging out
// // a line of print, or a disk drive with a formatting job in progress, it might
// // holdback for quite a while; there's no time limit.
// bool IRAM_ATTR JiffyDOS::sendByte(uint8_t data, bool eoi) {
//     //IEC_ASSERT(PIN_DEBUG);
//     portDISABLE_INTERRUPTS();

//     IEC.flags &= CLEAR_LOW;

//     // Release clock to signal we are ready
//     IEC_RELEASE(PIN_IEC_CLK_OUT);

//     // Wait for listener ready
//     while (IEC_IS_ASSERTED(PIN_IEC_DATA_IN)) {
//         if (IEC_IS_ASSERTED(PIN_IEC_ATN)) {
//             IEC.flags |= ATN_ASSERTED;
//             portENABLE_INTERRUPTS();
//             return false;
//         }
//     }
//     // if (waitForSignals(PIN_IEC_DATA_IN, IEC_RELEASED, PIN_IEC_ATN, IEC_ASSERTED,
//     //                    TIMEOUT_DEFAULT) == TIMED_OUT) {
//     //     IEC.flags |= ATN_ASSERTED;
//     //     portENABLE_INTERRUPTS();
//     //     return false;
//     // }

//     // STEP 2: SENDING THE BITS
//     // As soon as the listener releases the DATA line we are expected to send
//     // the bits Bits are inverted so use IEC_IS_ASSERTED() to get
//     // asserted/released status

//     // IEC_ASSERT( PIN_DEBUG );
//     timer_start();

//     // set bits 0,1
//     // IEC_ASSERT( PIN_DEBUG );
//     (data & (1 << 0)) ? IEC_RELEASE(PIN_IEC_CLK_OUT)
//                       : IEC_ASSERT(PIN_IEC_CLK_OUT);
//     (data & (1 << 1)) ? IEC_RELEASE(PIN_IEC_DATA_OUT)
//                       : IEC_ASSERT(PIN_IEC_DATA_OUT);
//     timer_wait_until(16.5);

//     // set bits 2,3
//     (data & (1 << 2)) ? IEC_RELEASE(PIN_IEC_CLK_OUT)
//                       : IEC_ASSERT(PIN_IEC_CLK_OUT);
//     (data & (1 << 3)) ? IEC_RELEASE(PIN_IEC_DATA_OUT)
//                       : IEC_ASSERT(PIN_IEC_DATA_OUT);
//     timer_wait_until(27.5);

//     // set bits 4,5
//     (data & (1 << 4)) ? IEC_RELEASE(PIN_IEC_CLK_OUT)
//                       : IEC_ASSERT(PIN_IEC_CLK_OUT);
//     (data & (1 << 5)) ? IEC_RELEASE(PIN_IEC_DATA_OUT)
//                       : IEC_ASSERT(PIN_IEC_DATA_OUT);
//     timer_wait_until(39);

//     // set bits 6,7
//     (data & (1 << 6)) ? IEC_RELEASE(PIN_IEC_CLK_OUT)
//                       : IEC_ASSERT(PIN_IEC_CLK_OUT);
//     (data & (1 << 7)) ? IEC_RELEASE(PIN_IEC_DATA_OUT)
//                       : IEC_ASSERT(PIN_IEC_DATA_OUT);
//     timer_wait_until(50);

//     // Check CLK for EOI
//     if (eoi) {
//         // This was the last byte
//         // CLK=HIGH and DATA=LOW  means EOI (this was the last byte)
//         // CLK=HIGH and DATA=HIGH means "error"
//         IEC_RELEASE(PIN_IEC_CLK_OUT);
//         IEC_ASSERT(PIN_IEC_DATA_OUT);
//     } else {
//         // More data to come
//         // CLK=LOW  and DATA=HIGH means "at least one more byte"
//         IEC_ASSERT(PIN_IEC_CLK_OUT);
//         IEC_RELEASE(PIN_IEC_DATA_OUT);
//     }

//     // EOI/error status is read by receiver 59 cycles after DATA HIGH (FBEF)
//     // receiver sets DATA low 63 cycles after initial DATA HIGH (FBF2)
//     timer_wait_until(60);
//     // IEC_RELEASE( PIN_DEBUG );

//     // Wait for listener to acknowledge of byte received
//     // while (!IEC_IS_ASSERTED(PIN_IEC_DATA_IN)) {
//     //     if (IEC_IS_ASSERTED(PIN_IEC_ATN)) {
//     //         IEC.flags |= ATN_ASSERTED;
//     //         goto done;
//     //     }
//     // }
//     if (waitForSignals(PIN_IEC_DATA_IN, IEC_ASSERTED, 0, 0,
//                        TIMEOUT_DEFAULT) == TIMED_OUT) {
//         portENABLE_INTERRUPTS();
//         return false;
//     }

//     // Debug_printv("data[%02X] eoi[%d]", data, eoi); // $ = 0x24

//     portENABLE_INTERRUPTS();

//     return true;
// }  // sendByte

// #endif  // JIFFYDOS
// #endif  // BUILD_IEC
