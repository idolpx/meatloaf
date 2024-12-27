// #ifdef BUILD_IEC
// #ifdef MEATLOAF_MAX
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

// #include "saucedos.h"

// #include <rom/ets_sys.h>
// // #include <freertos/FreeRTOS.h>
// // #include <freertos/task.h>
// // #include <freertos/semphr.h>
// // #include <driver/timer.h>

// #include "../../../include/cbm_defines.h"
// #include "../../../include/debug.h"
// #include "../../../include/pinmap.h"
// #include "_protocol.h"
// #include "bus.h"

// using namespace Protocol;

// uint8_t SauceDOS::receiveByte() {
//     int abort = 0;
//     uint8_t data = 0;

//     portDISABLE_INTERRUPTS();

//     IEC.flags &= CLEAR_LOW;

//     // RECEIVING THE BITS
//     // As soon as the talker releases the Clock line we are expected to receive
//     // the bits Bits are inverted so use IEC_IS_ASSERTED() to get
//     // asserted/released status

//     // SIGNAL WE ARE READY TO RECEIVE
//     IEC_RELEASE(PIN_IEC_CLK_OUT);

//     for (int idx = data = 0; !abort && idx < 7; idx++) {
//         if ((abort = waitForSignals(PIN_IEC_CLK_IN, IEC_RELEASED, 0, 0,
//                                     TIMEOUT_DEFAULT)))
//             break;

//         // Read First Bit
//         if (!IEC_IS_ASSERTED(PIN_IEC_DATA_IN)) data |= (1 << idx);

//         // Read Second Bit
//         idx++;
//         if (!IEC_IS_ASSERTED(PIN_IEC_SRQ)) data |= (1 << idx);

//         if (waitForSignals(PIN_IEC_CLK_IN, IEC_ASSERTED, 0, 0,
//                            TIMEOUT_DEFAULT)) {
//             if (idx < 7) abort = 1;
//         }
//     }

//     // Acknowledge byte received
//     // If we want to indicate an error we can release DATA
//     if (!waitForSignals(PIN_IEC_CLK_IN, IEC_ASSERTED, 0, 0, TIMEOUT_DEFAULT)) {
//         if (IEC_IS_ASSERTED(PIN_IEC_SRQ)) {
//             // Check CLK for EOI
//             IEC.flags |= EOI_RECVD;
//         }

//         IEC_ASSERT(PIN_IEC_DATA_OUT);
//         usleep(TIMING_Tv);
//         IEC_RELEASE(PIN_IEC_DATA_OUT);
//     }

//     if (mode == PROTOCOL_COMMAND) {
//         if (data & (1 << 8))
//             mode = PROTOCOL_LISTEN;
//         else
//             mode = PROTOCOL_TALK;
//     }

//     portENABLE_INTERRUPTS();

//     return data;
// }  // receiveByte

// // STEP 1: READY TO SEND
// // Sooner or later, the talker will want to talk, and send a
// // character. When it's ready to go, it releases the Clock line. This
// // signal change might be translated as "I'm ready to send a
// // character." The listener must detect this and respond, but it
// // doesn't have to do so immediately. The listener will respond to the
// // talker's "ready to send" signal whenever it likes; it can wait a
// // long time. If it's a printer chugging out a line of print, or a
// // disk drive with a formatting job in progress, it might holdback for
// // quite a while; there's no time limit.
// bool IRAM_ATTR SauceDOS::sendByte(uint8_t data, bool eoi) {
//     int len;
//     int abort = 0;

//     // IEC_ASSERT(PIN_IEC_SRQ);//Debug
//     gpio_intr_disable(PIN_IEC_CLK_IN);
//     portDISABLE_INTERRUPTS();

//     // SIGNAL WE READY TO SEND
//     IEC_RELEASE(PIN_IEC_CLK_OUT);

//     // Is CLOCK being streched?

//     // FIXME - Can't wait FOREVER because watchdog will get
//     //         mad. Probably need to configure DATA GPIO with POSEDGE
//     //         interrupt and not do portDISABLE_INTERRUPTS(). Without
//     //         interrupts disabled though there is a big risk of false
//     //         EOI being sent. Maybe the DATA ISR needs to handle EOI
//     //         signaling too?

//     if ((abort = waitForSignals(PIN_IEC_CLK_IN, IEC_RELEASED, PIN_IEC_ATN,
//                                 IEC_ASSERTED, FOREVER))) {
//         Debug_printv("data released abort");
//     }

//     /* Because interrupts are disabled it's possible to miss the ATN pause
//      * signal */
//     if (IEC_IS_ASSERTED(PIN_IEC_ATN)) {
//         abort = 1;
//         // Debug_printv("ATN abort");
//     }

//     // STEP 3: SEND THE BITS
//     for (int idx = 0; !abort && idx < 8; len++) {
//         // Have to make sure CLK is release before setting
//         IEC_RELEASE(PIN_IEC_CLK_OUT);

//         if (IEC_IS_ASSERTED(PIN_IEC_ATN)) {
//             // Debug_printv("ATN 2 abort");
//             abort = 1;
//             break;
//         }

//         // Set First Bit
//         if (data & (1 << idx)) {
//             IEC_RELEASE(PIN_IEC_DATA_OUT);

//             // Check to see if DATA is being pulled
//             // If so abort because of bus collision
//         } else
//             IEC_ASSERT(PIN_IEC_DATA_OUT);

//         // Set Second Bit
//         idx++;
//         if (data & (1 << idx))
//             IEC_RELEASE(PIN_IEC_SRQ);
//         else
//             IEC_ASSERT(PIN_IEC_SRQ);

//         usleep(TIMING_Tv);
//         IEC_RELEASE(PIN_IEC_DATA_OUT);
//         usleep(TIMING_Tv);
//         IEC_ASSERT(PIN_IEC_CLK_OUT);
//     }

//     // STEP 4: FRAME HANDSHAKE
//     // After the eighth bit has been sent, it's the listener's turn to
//     // acknowledge. At this moment, the Clock line is asserted and the
//     // Data line is released. The listener must acknowledge receiving
//     // the byte OK by asserting the Data line. The talker is now
//     // watching the Data line. If the listener doesn't assert the Data
//     // line within one millisecond - one thousand microseconds - it
//     // will know that something's wrong and may alarm appropriately.

//     // Wait for listener to accept data
//     IEC_RELEASE(PIN_IEC_CLK_OUT);

//     // Set EOI signal
//     if (eoi)
//         IEC_ASSERT(PIN_IEC_SRQ);
//     else
//         IEC_RELEASE(PIN_IEC_SRQ);

//     if (!abort &&
//         (abort = waitForSignals(PIN_IEC_DATA_IN, IEC_ASSERTED, PIN_IEC_ATN,
//                                 IEC_ASSERTED, TIMEOUT_Tf))) {
//         // RECIEVER TIMEOUT
//         // If no receiver asserts DATA within 1000 µs at the end of
//         // the transmission of a byte (after step 28), a receiver
//         // timeout is raised.
//         if (!IEC_IS_ASSERTED(PIN_IEC_ATN)) {
//             abort = 0;
//         } else {
//             IEC_SET_STATE(BUS_IDLE);
//         }
//     }
//     portENABLE_INTERRUPTS();
//     gpio_intr_enable(PIN_IEC_CLK_IN);
//     // IEC_RELEASE(PIN_DEBUG);//Debug

//     return !abort;
// }

// #endif  // MEATLOAF_MAX
// #endif  // BUILD_IEC
