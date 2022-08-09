/*  ----------------------------------------------------------------------------
    File: main.c
    Author(s):  Tiago Medicci Serrano <tiago.medicci@gmail.com>
    Date Created: 06/02/2018
    Last modified: 06/02/2018

    ------------------------------------------------------------------------- */

#include "feedback.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_task.h"
#include "esp_log.h"

#include "driver/gpio.h"

#include "sdkconfig.h"

#include "led_strip.h"

#include "../../include/pinmap.h"

static const char *TAG = "LED_STRIP";

struct led_strip_t led_strip;
struct led_strip_effect_t led_strip_effect;

struct effect_rgb_args_t rgb_effect = {
	.rgb_effect_state = 0,
	.speed = 255,
};

struct effect_static_color_args_t static_color = {
	.effect_color = {
		.red = 0,
		.green = 0,
		.blue = 0
	}
};

struct effect_timed_on_fade_out_args_t effect_timed_on_fade_out = {
	.counter = 0,
	.off_time_ms = 250,
	.fade_out_speed = 0,
	.step_counter = 0,
	.fade_step = 2,
    .effect_color = {0, 0, 0},
};


struct effect_timed_fade_in_off_args_t effect_timed_fade_in_off = {
	.counter = 0,
	.on_time_ms = 250,
	.fade_in_speed = 0,
	.step_counter = 0,
	.fade_step = 2,
    .effect_color = {0, 0, 0},
};


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
esp_err_t initialize_LED(void)
{
	esp_err_t ret = ESP_OK;

	static struct led_color_t led_strip_buf_1[CONFIG_LED_STRIP_LENGTH];
	static struct led_color_t led_strip_buf_2[CONFIG_LED_STRIP_LENGTH];


	led_strip.rgb_led_type = CONFIG_RGB_LED_TYPE;
	led_strip.rmt_channel = CONFIG_RMT_CHANNEL;
	led_strip.gpio = CONFIG_GPIO_NUM;
	led_strip.led_strip_buf_1 = led_strip_buf_1;
	led_strip.led_strip_buf_2 = led_strip_buf_2;
	led_strip.led_strip_length = CONFIG_LED_STRIP_LENGTH;
	led_strip.access_semaphore = xSemaphoreCreateBinary();

	//TODO: turn led_strip_init return into a esp_err_t indicating ESP-IDF error code
	if( (led_strip_init(&led_strip)) != true){
		ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
		return ESP_FAIL;
	}

	if( (ret = led_strip_init_effect_handler(&led_strip, CLEAR, NULL)) != ESP_OK)
	{
		ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
		return ret;
	}

	return ESP_OK;
}

void main_led_task(void *pv)
{
	ESP_ERROR_CHECK( initialize_LED() );
	ESP_LOGI(TAG, "Initializing effect handler...");
	vTaskDelay(2000 / portTICK_PERIOD_MS);														//Wait for 2s

	while(true){
		ESP_LOGI(TAG, "Setting Red, Green and Blue of static color effect to 0...");
		static_color.effect_color.red = 0;
		static_color.effect_color.green = 0;
		static_color.effect_color.blue = 0;

		// ESP_LOGI(TAG, "Setting Red of static color effect to 255...");
		// static_color.effect_color.red = 255;
		// ESP_ERROR_CHECK( led_strip_set_effect(&led_strip, COLOR, &static_color) );					//Set full red color
		// ESP_LOGI(TAG, "LED is full red...");

		// vTaskDelay(2000 / portTICK_PERIOD_MS);														//Wait for 2s
		// ESP_LOGI(TAG, "Setting Green of static color effect to 255...");
		// static_color.effect_color.green = 255;
		// ESP_ERROR_CHECK( led_strip_set_effect(&led_strip, COLOR, &static_color) );					//Set full red and green color (yellow)
		// ESP_LOGI(TAG, "LED is full yellow...");

		// vTaskDelay(2000 / portTICK_PERIOD_MS);														//Wait for 2s
		// ESP_LOGI(TAG, "Setting Blue of static color effect to 255...");
		// static_color.effect_color.blue = 255;
		// ESP_ERROR_CHECK( led_strip_set_effect(&led_strip, COLOR, &static_color) );					//Set full red, green and blue color (white)
		// ESP_LOGI(TAG, "LED is full white...");
		// vTaskDelay(2000 / portTICK_PERIOD_MS);														//Wait for 2s

		ESP_LOGI(TAG, "Setting RGB switching effect at max speed...");
		ESP_ERROR_CHECK( led_strip_set_effect(&led_strip, RGB, &rgb_effect) );						//Set RGB switching color
		vTaskDelay(2000 / portTICK_PERIOD_MS);														//Wait for 2s

		// ESP_LOGI(TAG, "Setting fade-out effect: Red = 20 for two times...");
		// effect_timed_on_fade_out.counter = 2;														//Execute for two times
		// effect_timed_on_fade_out.effect_color.red = 20;												//Starts red ON (20/255)
		// ESP_ERROR_CHECK( led_strip_set_effect(&led_strip, TIMED_ON_FADE_OUT, &effect_timed_on_fade_out) );
		// vTaskDelay(2000 / portTICK_PERIOD_MS);														//Wait for 2s

		// ESP_LOGI(TAG, "Setting fade-in effect: Red = 20 for two times...");
		// effect_timed_fade_in_off.counter = 2;														//Execute for two times
		// effect_timed_fade_in_off.effect_color.red = 20;												//Starts red off until (20/255)
		// ESP_ERROR_CHECK( led_strip_set_effect(&led_strip, TIMED_FADE_IN_OFF, &effect_timed_fade_in_off) );
		// vTaskDelay(2000 / portTICK_PERIOD_MS);														//Wait for 2s

		// ESP_LOGI(TAG, "Setting fade-out effect: Red, Green and Blue = 50 until next led_strip_set_effect command...");
		// effect_timed_on_fade_out.counter = 0;														//Execute until new led_strip_set_effect or led_strip_clear
		// effect_timed_on_fade_out.effect_color.green = 50;											//Starts green ON (50/255)
		// effect_timed_on_fade_out.effect_color.blue = 50;											//Starts blue ON (50/255)
		// ESP_ERROR_CHECK( led_strip_set_effect(&led_strip, TIMED_ON_FADE_OUT, &effect_timed_on_fade_out) );
		// vTaskDelay(5000 / portTICK_PERIOD_MS);

		// ESP_LOGI(TAG, "Setting fade-in effect: Red, Green and Blue = 50 until next led_strip_set_effect command...");
		// effect_timed_fade_in_off.counter = 0;														//Execute until new led_strip_set_effect or led_strip_clear
		// effect_timed_fade_in_off.effect_color.green = 50;											//Starts green off until (50/255)
		// effect_timed_fade_in_off.effect_color.blue = 50;											//Starts blue off until (50/255)
		// ESP_ERROR_CHECK( led_strip_set_effect(&led_strip, TIMED_FADE_IN_OFF, &effect_timed_fade_in_off) );
		// vTaskDelay(5000 / portTICK_PERIOD_MS);

		// ESP_LOGI(TAG, "LED is full white...");
		// ESP_ERROR_CHECK( led_strip_set_effect(&led_strip, COLOR, &static_color) );					//Set full red, green and blue color (white) again
		// vTaskDelay(1000 / portTICK_PERIOD_MS);

		// ESP_LOGI(TAG, "Clearing LED strip with led_strip_clear...");
		// led_strip_clear(&led_strip);																//Clears the led_strip.
		// vTaskDelay(1000 / portTICK_PERIOD_MS);

		// ESP_LOGI(TAG, "LED is full white...");
		// ESP_ERROR_CHECK( led_strip_set_effect(&led_strip, COLOR, &static_color) );					//Set full red, green and blue color (white) again
		// vTaskDelay(1000 / portTICK_PERIOD_MS);

		ESP_LOGI(TAG, "Deleting effect handler...");
		ESP_ERROR_CHECK( led_strip_delete_effect_handler(&led_strip) );								//Delete effect handler task
	}
}

void led_strip_main(void)
{
    esp_err_t ret = ESP_OK;
    TaskHandle_t main_task_handle = NULL;

    if( (ret = xTaskCreate(main_led_task, "main_led_task", 2048, NULL, 8, &main_task_handle)) != pdPASS){
		ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
	}

}

