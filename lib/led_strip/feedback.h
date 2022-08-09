/*  ----------------------------------------------------------------------------
    File: main.c
    Author(s):  Tiago Medicci Serrano <tiago.medicci@gmail.com>
    Date Created: 06/02/2018
    Last modified: 06/02/2018

    ------------------------------------------------------------------------- */

#ifndef FEEDBACK_H
#define FEEDBACK_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void main_led_task(void *args);

/**
  * @brief     	Initialize WS2812 LED Interface
  *
  * @param		None
  *
  * @return
  *      -ESP_OK 	On success
  *      -ESP_FAIL 	Generic code indicating failure
  *
  **/
esp_err_t initialize_LED(void);

void main_led_task(void *pv);

void led_strip_main(void);

#ifdef __cplusplus
}
#endif

#endif // FEEDBACK_H