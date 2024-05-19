#ifdef BUILD_IEC
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

#include "jiffydos.h"

#include <rom/ets_sys.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #include <freertos/semphr.h>
// #include <driver/timer.h>

#include "bus.h"
#include "_protocol.h"

#include "../../../include/debug.h"
#include "../../../include/pinmap.h"

using namespace Protocol;

// #define portTICK_PERIOD_NS(ns)  ((( TickType_t ) 1000000 / configTICK_RATE_HZ) / ns)

// // https://embeddedexplorer.com/esp32-timer-tutorial/
// static SemaphoreHandle_t s_timer_sem;
// static bool IRAM_ATTR timer_group_isr_callback(void * args)
// {
//     BaseType_t high_task_awoken = pdFALSE;
//     xSemaphoreGiveFromISR(s_timer_sem, &high_task_awoken);
//     return (high_task_awoken == pdTRUE);
// }

// STEP 1: READY TO RECEIVE
// Sooner or later, the talker will want to talk, and send a character.
// When it's ready to go, it releases the Clock line to false.  This signal change might be
// translated as "I'm ready to send a character." The listener must detect this and 
// immediately start receiving data on both the clock and data lines.
int16_t  JiffyDOS::receiveByte ()
{
    uint8_t data = 0;

    IEC.flags and_eq CLEAR_LOW;

    // Release the Data line to signal we are ready
#ifndef IEC_SPLIT_LINE
    IEC.release(PIN_IEC_CLK_IN);
    IEC.release(PIN_IEC_DATA_IN);
#endif

    // Wait for talker ready
    while ( IEC.status( PIN_IEC_CLK_IN ) == PULLED );

    // STEP 2: RECEIVING THE BITS
    // As soon as the talker releases the Clock line we are expected to receive the bits
    // Bits are inverted so use IEC.status() to get pulled/released status

    //IEC.pull ( PIN_IEC_SRQ );

    // Start timer
    uint64_t cur_time = esp_timer_get_time();
    uint64_t exp_time = 11;
    while (esp_timer_get_time() - cur_time < exp_time);

    // get bits 4,5
    IEC.pull ( PIN_IEC_SRQ );
    if ( IEC.status ( PIN_IEC_CLK_IN ) )  data |= 0b00010000; // 1
    if ( IEC.status ( PIN_IEC_DATA_IN ) ) data |= 0b00100000; // 0
    exp_time += bit_pair_timing[0][0];
    while (esp_timer_get_time() - cur_time < exp_time);

    // get bits 6,7
    if ( IEC.status ( PIN_IEC_CLK_IN ) ) data |=  0b01000000; // 0
    if ( IEC.status ( PIN_IEC_DATA_IN ) ) data |= 0b10000000; // 0
    exp_time += bit_pair_timing[0][1];
    while (esp_timer_get_time() - cur_time < exp_time);

    // get bits 3,1
    if ( IEC.status ( PIN_IEC_CLK_IN ) )  data |= 0b00001000; // 0
    if ( IEC.status ( PIN_IEC_DATA_IN ) ) data |= 0b00000010; // 0
    exp_time += bit_pair_timing[0][2];
    while (esp_timer_get_time() - cur_time < exp_time);

    // get bits 2,0
    if ( IEC.status ( PIN_IEC_CLK_IN ) )  data |= 0b00000100; // 1
    if ( IEC.status ( PIN_IEC_DATA_IN ) ) data |= 0b00000001; // 0
    exp_time += bit_pair_timing[0][3];
    while (esp_timer_get_time() - cur_time < exp_time);

    // Acknowledge byte received
    // If we want to indicate an error we can release DATA
    IEC.pull ( PIN_IEC_DATA_OUT );

    // Check CLK for EOI
    bool eoi = gpio_get_level ( PIN_IEC_CLK_IN );
    exp_time += 15;
    while (esp_timer_get_time() - cur_time < exp_time);
    IEC.release ( PIN_IEC_SRQ );
    wait( 148 );

    if ( eoi ) IEC.flags |= EOI_RECVD;
    //Debug_printv("data[%02X] eoi[%d]", data, eoi); // $ = 0x24

    return (uint8_t) (data & 0xFF);
} // receiveByte


// STEP 1: READY TO SEND
// Sooner or later, the talker will want to talk, and send a character.
// When it's ready to go, it releases the Clock line to false.  This signal change might be
// translated as "I'm ready to send a character." The listener must detect this and respond,
// but it doesn't have to do so immediately. The listener will respond  to  the  talker's
// "ready  to  send"  signal  whenever  it  likes;  it  can  wait  a  long  time.    If  it's
// a printer chugging out a line of print, or a disk drive with a formatting job in progress,
// it might holdback for quite a while; there's no time limit.
bool JiffyDOS::sendByte ( uint8_t data, bool signalEOI )
{
    IEC.flags and_eq CLEAR_LOW;

    // Release the Data line to signal we are ready
#ifndef IEC_SPLIT_LINE
    IEC.release(PIN_IEC_DATA_IN);
#endif

    // Wait for listener ready
    IEC.release ( PIN_IEC_CLK_OUT );
    while ( IEC.status( PIN_IEC_DATA_OUT ) == RELEASED );

    // STEP 2: SENDING THE BITS
    // As soon as the listener releases the DATA line we are expected to send the bits
    // Bits are inverted so use IEC.status() to get pulled/released status

    //IEC.pull ( PIN_IEC_SRQ );

    // Start timer
    uint64_t cur_time = esp_timer_get_time();
    uint64_t exp_time = 11;
    while (esp_timer_get_time() - cur_time < exp_time);

    // set bits 0,1
    IEC.pull ( PIN_IEC_SRQ );
    ( data & 0b00000001 ) ? IEC.release ( PIN_IEC_CLK_OUT ) : IEC.pull ( PIN_IEC_CLK_OUT );
    ( data & 0b00000010 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
    exp_time += bit_pair_timing[1][0];
    while (esp_timer_get_time() - cur_time < exp_time);

    // set bits 2,3
    ( data & 0b00000100 ) ? IEC.release ( PIN_IEC_CLK_OUT ) : IEC.pull ( PIN_IEC_CLK_OUT );
    ( data & 0b00001000 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
    exp_time += bit_pair_timing[1][1];
    while (esp_timer_get_time() - cur_time < exp_time);

    // set bits 4,5
    ( data & 0b00010000 ) ? IEC.release ( PIN_IEC_CLK_OUT ) : IEC.pull ( PIN_IEC_CLK_OUT );
    ( data & 0b00100000 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
    exp_time += bit_pair_timing[1][2];
    while (esp_timer_get_time() - cur_time < exp_time);

    // set bits 6,7
    ( data & 0b01000000 ) ? IEC.release ( PIN_IEC_CLK_OUT ) : IEC.pull ( PIN_IEC_CLK_OUT );
    ( data & 0b10000000 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
    exp_time += bit_pair_timing[1][3];
    while (esp_timer_get_time() - cur_time < exp_time);

    // Acknowledge byte received
    // If we want to indicate an error we can release DATA
    bool error = IEC.status ( PIN_IEC_DATA_IN );

    // Check CLK for EOI
    ( signalEOI ) ? IEC.pull ( PIN_IEC_CLK_OUT ) : IEC.release ( PIN_IEC_CLK_OUT );
    exp_time += 13;
    while (esp_timer_get_time() - cur_time < exp_time);
    IEC.release ( PIN_IEC_SRQ );

    return true;
} // sendByte

#endif /* BUILD_IEC*/