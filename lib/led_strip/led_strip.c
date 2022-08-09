/*  ----------------------------------------------------------------------------
    File: led_strip.c
    Author(s):  Lucas Bruder <LBruder@me.com>
    Date Created: 11/23/2016
    Last modified: 11/26/2016

    Description: LED Library for driving various led strips on ESP32.

    This library uses double buffering to display the LEDs.
    If the driver is showing buffer 1, any calls to led_strip_set_pixel_color
    will write to buffer 2. When it's time to drive the pixels on the strip, it
    refers to buffer 1. 
    When led_strip_show is called, it will switch to displaying the pixels
    from buffer 2 and will clear buffer 1. Any writes will now happen on buffer 1 
    and the task will look at buffer 2 for refreshing the LEDs
    ------------------------------------------------------------------------- */

#include "led_strip.h"

#define LED_STRIP_TASK_SIZE             2*1024
#define LED_STRIP_TASK_PRIORITY         (configMAX_PRIORITIES - 1)

#define LED_STRIP_REFRESH_PERIOD_MS     (30U) // TODO: add as parameter to led_strip_init
#define EFFECT_CHANGE_CHECK_PERIOD_MS	(500U)//Period to check the effect parameters has changed

#define LED_STRIP_NUM_RMT_ITEMS_PER_LED (24U) // Assumes 24 bit color for each led

#define LED_STRIP_EFFECT_TASK_SIZE     	4*1024
#define LED_STRIP_EFFECT_TASK_PRIORITY 	(configMAX_PRIORITIES - 2)

// RMT Clock source is @ 80 MHz. Dividing it by 8 gives us 10 MHz frequency, or 100ns period.
#define LED_STRIP_RMT_CLK_DIV (8)

/****************************
        WS2812 Timing
 ****************************/
#define LED_STRIP_RMT_TICKS_BIT_1_HIGH_WS2812 9 // 900ns (900ns +/- 150ns per datasheet)
#define LED_STRIP_RMT_TICKS_BIT_1_LOW_WS2812  3 // 300ns (350ns +/- 150ns per datasheet)
#define LED_STRIP_RMT_TICKS_BIT_0_HIGH_WS2812 3 // 300ns (350ns +/- 150ns per datasheet)
#define LED_STRIP_RMT_TICKS_BIT_0_LOW_WS2812  9 // 900ns (900ns +/- 150ns per datasheet)

/****************************
        SK6812 Timing
 ****************************/
#define LED_STRIP_RMT_TICKS_BIT_1_HIGH_SK6812 6
#define LED_STRIP_RMT_TICKS_BIT_1_LOW_SK6812  6
#define LED_STRIP_RMT_TICKS_BIT_0_HIGH_SK6812 3
#define LED_STRIP_RMT_TICKS_BIT_0_LOW_SK6812  9

/****************************
        APA106 Timing
 ****************************/
#define LED_STRIP_RMT_TICKS_BIT_1_HIGH_APA106 14 // 1.36us +/- 150ns per datasheet
#define LED_STRIP_RMT_TICKS_BIT_1_LOW_APA106   3 // 350ns +/- 150ns per datasheet
#define LED_STRIP_RMT_TICKS_BIT_0_HIGH_APA106  3 // 350ns +/- 150ns per datasheet
#define LED_STRIP_RMT_TICKS_BIT_0_LOW_APA106  14 // 1.36us +/- 150ns per datasheet

const char *TAG = "LED_STRIP";

static TaskHandle_t led_strip_effect_task_handle = NULL;
static xQueueHandle effect_queue_handle = NULL;

// Function pointer for generating waveforms based on different LED drivers
typedef void (*led_fill_rmt_items_fn)(struct led_color_t *led_strip_buf, rmt_item32_t *rmt_items, uint32_t led_strip_length);

static inline void led_strip_fill_item_level(rmt_item32_t* item, int high_ticks, int low_ticks)
{
    item->level0 = 1;
    item->duration0 = high_ticks;
    item->level1 = 0;
    item->duration1 = low_ticks;
}

static inline void led_strip_rmt_bit_1_sk6812(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_1_HIGH_SK6812, LED_STRIP_RMT_TICKS_BIT_1_LOW_SK6812);
}

static inline void led_strip_rmt_bit_0_sk6812(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_0_HIGH_SK6812, LED_STRIP_RMT_TICKS_BIT_0_LOW_SK6812);
}

static void led_strip_fill_rmt_items_sk6812(struct led_color_t *led_strip_buf, rmt_item32_t *rmt_items, uint32_t led_strip_length)
{
    uint32_t rmt_items_index = 0;
    for (uint32_t led_index = 0; led_index < led_strip_length; led_index++) {
        struct led_color_t led_color = led_strip_buf[led_index];

        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.green >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_sk6812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_sk6812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.red >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_sk6812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_sk6812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.blue >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_sk6812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_sk6812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
    }
}

static inline void led_strip_rmt_bit_1_ws2812(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_1_HIGH_WS2812, LED_STRIP_RMT_TICKS_BIT_1_LOW_WS2812);
}

static inline void led_strip_rmt_bit_0_ws2812(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_0_HIGH_WS2812, LED_STRIP_RMT_TICKS_BIT_0_LOW_WS2812);
}

static void led_strip_fill_rmt_items_ws2812(struct led_color_t *led_strip_buf, rmt_item32_t *rmt_items, uint32_t led_strip_length)
{
    uint32_t rmt_items_index = 0;
    for (uint32_t led_index = 0; led_index < led_strip_length; led_index++) {
        struct led_color_t led_color = led_strip_buf[led_index];

        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.green >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_ws2812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_ws2812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.red >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_ws2812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_ws2812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.blue >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_ws2812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_ws2812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
    }
}

static inline void led_strip_rmt_bit_1_apa106(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_1_HIGH_APA106, LED_STRIP_RMT_TICKS_BIT_1_LOW_APA106);
}

static inline void led_strip_rmt_bit_0_apa106(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_0_HIGH_APA106, LED_STRIP_RMT_TICKS_BIT_0_LOW_APA106);
}

static void led_strip_fill_rmt_items_apa106(struct led_color_t *led_strip_buf, rmt_item32_t *rmt_items, uint32_t led_strip_length)
{
    uint32_t rmt_items_index = 0;
    for (uint32_t led_index = 0; led_index < led_strip_length; led_index++) {
        struct led_color_t led_color = led_strip_buf[led_index];

        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.red >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_apa106(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_apa106(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.green >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_apa106(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_apa106(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.blue >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_apa106(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_apa106(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
    }
}

static void led_strip_task(void *arg)
{
    struct led_strip_t *led_strip = (struct led_strip_t *)arg;
    led_fill_rmt_items_fn led_make_waveform = NULL;
    bool make_new_rmt_items = true;
    bool prev_showing_buf_1 = !led_strip->showing_buf_1;

    size_t num_items_malloc = (LED_STRIP_NUM_RMT_ITEMS_PER_LED * led_strip->led_strip_length);
    rmt_item32_t *rmt_items = (rmt_item32_t*) malloc(sizeof(rmt_item32_t) * num_items_malloc);
    if (!rmt_items) {
        vTaskDelete(NULL);
    }

    switch (led_strip->rgb_led_type) {
        case RGB_LED_TYPE_WS2812:
            led_make_waveform = led_strip_fill_rmt_items_ws2812;
            break;

        case RGB_LED_TYPE_SK6812:
            led_make_waveform = led_strip_fill_rmt_items_sk6812;
            break;

        case RGB_LED_TYPE_APA106:
            led_make_waveform = led_strip_fill_rmt_items_apa106;
            break;

        default:
            // Will avoid keeping it point to NULL
            led_make_waveform = led_strip_fill_rmt_items_ws2812;
            break;
    };

    for(;;) {
        rmt_wait_tx_done(led_strip->rmt_channel, portMAX_DELAY);
        xSemaphoreTake(led_strip->access_semaphore, portMAX_DELAY);

        /*
         * If buf 1 was previously being shown and now buf 2 is being shown,
         * it should update the new rmt items array. If buf 2 was previous being shown
         * and now buf 1 is being shown, it should update the new rmt items array.
         * Otherwise, no need to update the array
         */
        if ((prev_showing_buf_1 == true) && (led_strip->showing_buf_1 == false)) {
            make_new_rmt_items = true;
        } else if ((prev_showing_buf_1 == false) && (led_strip->showing_buf_1 == true)) {
            make_new_rmt_items = true;
        } else {
            make_new_rmt_items = false;
        }

        if (make_new_rmt_items) {
            if (led_strip->showing_buf_1) {
                led_make_waveform(led_strip->led_strip_buf_1, rmt_items, led_strip->led_strip_length);
            } else {
                led_make_waveform(led_strip->led_strip_buf_2, rmt_items, led_strip->led_strip_length);
            }
        }

        rmt_write_items(led_strip->rmt_channel, rmt_items, num_items_malloc, false);
        prev_showing_buf_1 = led_strip->showing_buf_1;
        xSemaphoreGive(led_strip->access_semaphore);
        vTaskDelay(LED_STRIP_REFRESH_PERIOD_MS / portTICK_PERIOD_MS);
    }

    if (rmt_items) {
        free(rmt_items);
    }
    vTaskDelete(NULL);
}

static bool led_strip_init_rmt(struct led_strip_t *led_strip)
{
    rmt_config_t rmt_cfg = {
        .rmt_mode = RMT_MODE_TX,
        .channel = led_strip->rmt_channel,
        .clk_div = LED_STRIP_RMT_CLK_DIV,
        .gpio_num = led_strip->gpio,
        .mem_block_num = 1,
        .tx_config = {
            .loop_en = false,
            .carrier_freq_hz = 100, // Not used, but has to be set to avoid divide by 0 err
            .carrier_duty_percent = 50,
            .carrier_level = RMT_CARRIER_LEVEL_LOW,
            .carrier_en = false,
            .idle_level = RMT_IDLE_LEVEL_LOW,
            .idle_output_en = true,
        }
    };

    esp_err_t cfg_ok = rmt_config(&rmt_cfg);
    if (cfg_ok != ESP_OK) {
        return false;
    }
    esp_err_t install_ok = rmt_driver_install(rmt_cfg.channel, 0, 0);
    if (install_ok != ESP_OK) {
        return false;
    }

    return true;
}

bool led_strip_init(struct led_strip_t *led_strip)
{
    TaskHandle_t led_strip_task_handle;

    if ((led_strip == NULL) ||
        (led_strip->rmt_channel == RMT_CHANNEL_MAX) ||
        (led_strip->gpio > GPIO_NUM_33) ||  // only inputs above 33
        (led_strip->led_strip_buf_1 == NULL) ||
        (led_strip->led_strip_buf_2 == NULL) ||
        (led_strip->led_strip_length == 0) ||
        (led_strip->access_semaphore == NULL)) {
        return false;
    }

    if(led_strip->led_strip_buf_1 == led_strip->led_strip_buf_2) {
        return false;
    }

    memset(led_strip->led_strip_buf_1, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);
    memset(led_strip->led_strip_buf_2, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);

    bool init_rmt = led_strip_init_rmt(led_strip);
    if (!init_rmt) {
        return false;
    }

    xSemaphoreGive(led_strip->access_semaphore);
    BaseType_t task_created = xTaskCreate(led_strip_task,
                                            "led_strip_task",
                                            LED_STRIP_TASK_SIZE,
                                            led_strip,
                                            LED_STRIP_TASK_PRIORITY,
                                            &led_strip_task_handle
                                         );

    if (!task_created) {
        return false;
    }

    led_strip_effect_task_handle = NULL;

    return true;
}

bool led_strip_set_pixel_color(struct led_strip_t *led_strip, uint32_t pixel_num, struct led_color_t *color)
{
    bool set_led_success = true;

    if ((!led_strip) || (!color) || (pixel_num > led_strip->led_strip_length)) {
        return false;
    }

    if (led_strip->showing_buf_1) {
        led_strip->led_strip_buf_2[pixel_num] = *color;
    } else {
        led_strip->led_strip_buf_1[pixel_num] = *color;
    }

    return set_led_success;
}

bool led_strip_set_pixel_rgb(struct led_strip_t *led_strip, uint32_t pixel_num, uint8_t red, uint8_t green, uint8_t blue)
{
    bool set_led_success = true;

    if ((!led_strip) || (pixel_num > led_strip->led_strip_length)) {
        return false;
    }

    if (led_strip->showing_buf_1) {
        led_strip->led_strip_buf_2[pixel_num].red = red;
        led_strip->led_strip_buf_2[pixel_num].green = green;
        led_strip->led_strip_buf_2[pixel_num].blue = blue;
    } else {
        led_strip->led_strip_buf_1[pixel_num].red = red;
        led_strip->led_strip_buf_1[pixel_num].green = green;
        led_strip->led_strip_buf_1[pixel_num].blue = blue;
    }

    return set_led_success;
}

bool led_strip_get_pixel_color(struct led_strip_t *led_strip, uint32_t pixel_num, struct led_color_t *color)
{
    bool get_success = true;

    if ((!led_strip) ||
        (pixel_num > led_strip->led_strip_length) ||
        (!color)) {
        color = NULL;
        return false;
    }

    if (led_strip->showing_buf_1) {
        *color = led_strip->led_strip_buf_1[pixel_num];
    } else {
        *color = led_strip->led_strip_buf_2[pixel_num];
    }

    return get_success;
}

/**
 * Updates the led buffer to be shown
 */
bool led_strip_show(struct led_strip_t *led_strip)
{
    bool success = true;

    if (!led_strip) {
        return false;
    }

    xSemaphoreTake(led_strip->access_semaphore, portMAX_DELAY);
    if (led_strip->showing_buf_1) {
        led_strip->showing_buf_1 = false;
        memset(led_strip->led_strip_buf_1, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);
    } else {
        led_strip->showing_buf_1 = true;
        memset(led_strip->led_strip_buf_2, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);
    }
    xSemaphoreGive(led_strip->access_semaphore);

    return success;
}

/**
 * Clears the LED strip
 */
bool led_strip_clear(struct led_strip_t *led_strip)
{
    bool success = true;

    if (!led_strip) {
        return false;
    }

    if(led_strip_effect_task_handle != NULL)							//Check if the effect task is executing
	{
    	led_strip_set_effect(led_strip, CLEAR, NULL);
	}else
	{
		if (led_strip->showing_buf_1) {
			memset(led_strip->led_strip_buf_2, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);

		} else {
			memset(led_strip->led_strip_buf_1, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);
		}

		led_strip_show(led_strip);
	}

    return success;
}




static void led_strip_effect_task(void *arg)
{
    struct led_strip_effect_t received_led_strip_effect_from_queue;
	int data_received;
	struct effect_rgb_args_t *effect_rgb_args = NULL;
	struct effect_static_color_args_t *effect_static_color_args = NULL;
	struct effect_timed_on_fade_out_args_t *effect_timed_on_fade_out_args = NULL;
	struct effect_timed_fade_in_off_args_t *effect_timed_fade_in_off_args = NULL;
	struct led_color_t effect_color;
	uint16_t index;
	uint16_t internal_counter = 0;
	int64_t temp_microseconds = 0;

	memset(&curent_led_strip_effect, 0, sizeof(struct led_strip_effect_t));
	memset(&received_led_strip_effect_from_queue, 0, sizeof(struct led_strip_effect_t));

    while(true)
    {
    	/* If it has received new data from queue, free args from all effects */
        if( (data_received = xQueueReceive(effect_queue_handle, &received_led_strip_effect_from_queue, 0)) == pdTRUE )
        {
        	/* check if there is any difference between current curent_led_strip_effect and received_led_strip_effect_from_queue */
        	if(received_led_strip_effect_from_queue.restart_effect)
        	{
        		ESP_LOGI(TAG, "New effect data available!");
        		memcpy(&curent_led_strip_effect, &received_led_strip_effect_from_queue, sizeof(struct led_strip_effect_t));
        		/* If so, refresh effect configuration */
        		switch (curent_led_strip_effect.effect_type) {
					case RGB:
						effect_rgb_args = (struct effect_rgb_args_t *)curent_led_strip_effect.effect_args;
						ESP_LOGD(TAG, "Effect: RGB. Speed = %d", effect_rgb_args->speed);
						break;
					case COLOR:
						effect_static_color_args = (struct effect_static_color_args_t *)curent_led_strip_effect.effect_args;
						ESP_LOGD(TAG, "Effect: Static Color. Color = %d,%d,%d"
								,effect_static_color_args->effect_color.red
								,effect_static_color_args->effect_color.green
								,effect_static_color_args->effect_color.blue);
						break;
					case TIMED_ON_FADE_OUT:
						effect_timed_on_fade_out_args = (struct effect_timed_on_fade_out_args_t *)curent_led_strip_effect.effect_args;
						effect_timed_on_fade_out_args->step_counter =
								max(max(effect_timed_on_fade_out_args->effect_color.red,effect_timed_on_fade_out_args->effect_color.green),effect_timed_on_fade_out_args->effect_color.blue);
						temp_microseconds = esp_timer_get_time() - (effect_timed_on_fade_out_args->off_time_ms)*1000;
						/* copy RGB value to current effect_color at the initial state */
						effect_color.red = effect_timed_on_fade_out_args->effect_color.red;
						effect_color.green = effect_timed_on_fade_out_args->effect_color.green;
						effect_color.blue = effect_timed_on_fade_out_args->effect_color.blue;
						if(effect_timed_on_fade_out_args->counter == 0)
						{
							internal_counter = 1;
						}else
						{
							internal_counter = effect_timed_on_fade_out_args->counter;
						}
						ESP_LOGD(TAG, "Effect: TIMED_ON_FADE_OUT.\n Color = %d,%d,%d\n Fade In Time = %d\n Counter: %d"
								,effect_timed_on_fade_out_args->effect_color.red
								,effect_timed_on_fade_out_args->effect_color.green
								,effect_timed_on_fade_out_args->effect_color.blue
								,effect_timed_on_fade_out_args->fade_out_speed
								,effect_timed_on_fade_out_args->counter);
						break;
					case TIMED_FADE_IN_OFF:
						effect_timed_fade_in_off_args = (struct effect_timed_fade_in_off_args_t *)curent_led_strip_effect.effect_args;
						temp_microseconds = esp_timer_get_time() - (effect_timed_fade_in_off_args->on_time_ms)*1000;
						effect_timed_fade_in_off_args->step_counter = 0;
						effect_color.red = 0;
						effect_color.green = 0;
						effect_color.blue = 0;
						if(effect_timed_fade_in_off_args->counter == 0)
						{
							internal_counter = 1;
						}else
						{
							internal_counter = effect_timed_fade_in_off_args->counter;
						}
						ESP_LOGD(TAG, "Effect: TIMED_FADE_IN_OFF.\n Color = %d,%d,%d\n Fade In Time = %d\n Counter: %d"
								,effect_timed_fade_in_off_args->effect_color.red
								,effect_timed_fade_in_off_args->effect_color.green
								,effect_timed_fade_in_off_args->effect_color.blue
								,effect_timed_fade_in_off_args->fade_in_speed
								,effect_timed_fade_in_off_args->counter);
						break;
					case CLEAR:
						break;

					default:
						break;
				};
        		received_led_strip_effect_from_queue.restart_effect = false;
        	}
        }else /*Otherwise, if there is no new data on Queue, */
        {
			switch (curent_led_strip_effect.effect_type) {
				case RGB:
					switch(effect_rgb_args->rgb_effect_state)
					{
						case ALL_RED:
							effect_color.red = 255;
							effect_color.green = 0;
							effect_color.blue = 0;
							effect_rgb_args->rgb_effect_state++;
							break;
						case ALL_GREEN:
							effect_color.red = 0;
							effect_color.green = 255;
							effect_color.blue = 0;
							effect_rgb_args->rgb_effect_state++;
							break;
						case ALL_BLUE:
							effect_color.red = 0;
							effect_color.green = 0;
							effect_color.blue = 255;
							effect_rgb_args->rgb_effect_state=ALL_RED;
							break;
						default:
							effect_rgb_args->rgb_effect_state=ALL_RED;
							break;
					}
					for (uint16_t index = 0; index < curent_led_strip_effect.led_strip->led_strip_length; index++) {
						led_strip_set_pixel_color(curent_led_strip_effect.led_strip, index, &effect_color);
					}
					led_strip_show(curent_led_strip_effect.led_strip);
					vTaskDelay((10000-39*effect_rgb_args->speed)/portTICK_RATE_MS);
					break;
				case COLOR:
					for (uint16_t index = 0; index < curent_led_strip_effect.led_strip->led_strip_length; index++) {
						led_strip_set_pixel_color(curent_led_strip_effect.led_strip, index, &effect_static_color_args->effect_color);
					}
					led_strip_show(curent_led_strip_effect.led_strip);
					vTaskDelay(LED_STRIP_REFRESH_PERIOD_MS / portTICK_PERIOD_MS);
					break;
				case TIMED_ON_FADE_OUT:
					if(internal_counter > 0)
					{
						if( (esp_timer_get_time() - temp_microseconds) > (effect_timed_on_fade_out_args->off_time_ms*1000))
						{
							if(effect_color.red > 0
									|| effect_color.green > 0
									|| effect_color.blue > 0)
							{
								if(effect_color.red >= effect_timed_on_fade_out_args->fade_step)
									effect_color.red-=effect_timed_on_fade_out_args->fade_step;
								else
									effect_color.red = 0;
								if(effect_color.green >= effect_timed_on_fade_out_args->fade_step)
									effect_color.green-=effect_timed_on_fade_out_args->fade_step;
								else
									effect_color.green = 0;
								if(effect_color.blue >= effect_timed_on_fade_out_args->fade_step)
									effect_color.blue-=effect_timed_on_fade_out_args->fade_step;
								else
									effect_color.blue = 0;
								effect_timed_on_fade_out_args->step_counter =
										max(max(effect_color.red, effect_color.green),effect_color.blue);
								if(effect_timed_on_fade_out_args->step_counter == 0)
								{
									temp_microseconds = esp_timer_get_time();
								}
							}else
							{
								/* If effect counter is 0, run continually. Otherwise, decrease internal counter and when it reaches 0, clear the led strip */
								if(effect_timed_on_fade_out_args->counter != 0)
									internal_counter--;

								/* check if internal counter is different from 0, if not, do not reload effect_timed_on_fade_out_args->effect_color to effect_color*/
								if(internal_counter != 0)
								{
									/* copy RGB value to current effect_color at the initial state */
									effect_color.red = effect_timed_on_fade_out_args->effect_color.red;
									effect_color.green = effect_timed_on_fade_out_args->effect_color.green;
									effect_color.blue = effect_timed_on_fade_out_args->effect_color.blue;
								}
							}
						}
					}

					if( internal_counter == 0 )
					{
						curent_led_strip_effect.effect_type = CLEAR;
						curent_led_strip_effect.effect_args = NULL;
					}
					for (index = 0; index < curent_led_strip_effect.led_strip->led_strip_length; index++) {
						led_strip_set_pixel_color(curent_led_strip_effect.led_strip, index, &effect_color);
					}
					led_strip_show(curent_led_strip_effect.led_strip);
					vTaskDelay((effect_timed_on_fade_out_args->fade_out_speed + LED_STRIP_REFRESH_PERIOD_MS)/portTICK_RATE_MS);
					break;
				case TIMED_FADE_IN_OFF:
					if(internal_counter > 0)
					{
						if( (esp_timer_get_time() - temp_microseconds) > (effect_timed_fade_in_off_args->on_time_ms*1000))
						{
							if(effect_color.red < (effect_timed_fade_in_off_args->effect_color.red)
									|| effect_color.green < (effect_timed_fade_in_off_args->effect_color.green)
									|| effect_color.blue < (effect_timed_fade_in_off_args->effect_color.blue) )
							{
								if(effect_color.red <= (effect_timed_fade_in_off_args->effect_color.red - effect_timed_fade_in_off_args->fade_step) )
									effect_color.red+=effect_timed_fade_in_off_args->fade_step;
								else
									effect_color.red = effect_timed_fade_in_off_args->effect_color.red;
								if(effect_color.green <= (effect_timed_fade_in_off_args->effect_color.green - effect_timed_fade_in_off_args->fade_step) )
									effect_color.green+=effect_timed_fade_in_off_args->fade_step;
								else
									effect_color.green = effect_timed_fade_in_off_args->effect_color.green;
								if(effect_color.blue <= (effect_timed_fade_in_off_args->effect_color.blue - effect_timed_fade_in_off_args->fade_step) )
									effect_color.blue+=effect_timed_fade_in_off_args->fade_step;
								else
									effect_color.blue = effect_timed_fade_in_off_args->effect_color.blue;

								effect_timed_fade_in_off_args->step_counter =
										max(max(effect_color.red, effect_color.green),effect_color.blue);
								if(effect_timed_fade_in_off_args->step_counter ==
										max(max(effect_timed_fade_in_off_args->effect_color.red,effect_timed_fade_in_off_args->effect_color.green),effect_timed_fade_in_off_args->effect_color.blue)
									)
								{
									temp_microseconds = esp_timer_get_time();
								}
							}else
							{
								/* If effect counter is 0, run continually. Otherwise, decrease internal counter and when it reaches 0, clear the led strip */
								if(effect_timed_fade_in_off_args->counter != 0)
									internal_counter--;

								/* check if internal counter is different from 0, if not, do not reload effect_timed_on_fade_out_args->effect_color to effect_color*/
								if(internal_counter != 0)
								{
									/* copy RGB value to current effect_color at the initial state */
									effect_color.red = 0;
									effect_color.green = 0;
									effect_color.blue = 0;
								}
							}
						}
					}

					if( internal_counter == 0 )
					{
						curent_led_strip_effect.effect_type = CLEAR;
						curent_led_strip_effect.effect_args = NULL;
					}
					for (index = 0; index < curent_led_strip_effect.led_strip->led_strip_length; index++) {
						led_strip_set_pixel_color(curent_led_strip_effect.led_strip, index, &effect_color);
					}
					led_strip_show(curent_led_strip_effect.led_strip);
					vTaskDelay((effect_timed_fade_in_off_args->fade_in_speed + LED_STRIP_REFRESH_PERIOD_MS)/portTICK_RATE_MS);
					break;
				case CLEAR:
					memset(&effect_color, 0, sizeof(struct led_color_t));
					for (uint16_t index = 0; index < curent_led_strip_effect.led_strip->led_strip_length; index++) {
						led_strip_set_pixel_color(curent_led_strip_effect.led_strip, index, &effect_color);
					}
					led_strip_show(curent_led_strip_effect.led_strip);
					vTaskDelay(LED_STRIP_REFRESH_PERIOD_MS / portTICK_PERIOD_MS);
					break;

				default:
					vTaskDelete(NULL);
					break;
			}
        }
    }

    vTaskDelete(NULL);
}


/**
  * @brief     	Initialize task to handle LED strip effects
  *
  * @param 		pointer to led_strip strucutre
  * @param 		effect_type enum for pre-defined effects
  * @param 		arguments of the effect type
  *
  * @return
  *      -ESP_OK 	On success
  *      -ESP_FAIL 	Generic code indicating failure
  *      -ESP_ERR_INVALID_STATE if task (and led strip) has been already initialized
  *
  **/
esp_err_t led_strip_init_effect_handler(struct led_strip_t *led_strip, effect_type_t effect_type, void *effect_arg)
{
	struct led_strip_effect_t initial_led_strip_effect;
	esp_err_t ret = ESP_OK;

	/* Check if effect queue was not previously created */
	if(effect_queue_handle == NULL)
	{
		if( (effect_queue_handle = xQueueCreate(10, sizeof(struct led_strip_effect_t))) == pdFALSE )
		{
			ret = ESP_FAIL;
			ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
			return ret;
		}
	}else /* If effect queue was previously created, indicates an error */
	{
		ret = ESP_ERR_INVALID_STATE;
		ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
		return ret;
	}

	initial_led_strip_effect.led_strip = led_strip;
	initial_led_strip_effect.effect_type = effect_type;
	initial_led_strip_effect.effect_args = effect_arg;
	initial_led_strip_effect.restart_effect = true;

	if( xQueueSend( effect_queue_handle, ( void * ) &initial_led_strip_effect, ( TickType_t ) 0 ) != pdPASS )
	{
		ret = ESP_FAIL;
		ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
		return ret;
	}

	if(led_strip_effect_task_handle == NULL)
	{
		if (xTaskCreate(led_strip_effect_task,
						"led_strip_effect_task",
						LED_STRIP_EFFECT_TASK_SIZE,
						NULL,
						LED_STRIP_TASK_PRIORITY-1,
						&led_strip_effect_task_handle)
		!= pdTRUE)
		{
			ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
			return ret = ESP_FAIL;
			//FIXME: if task fails to initialize, clear queue before returning
		}
	}else
	{
		ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
		return ret = ESP_ERR_INVALID_STATE;
	}

	return ESP_OK;
}

/**
  * @brief     	Initialize task to create pre-defined effects
  *
  * @param 		led_strip_effect pointer to LED effect context
  * @param 		effect_type enum for pre-defined effects
  * @param 		effect_speed overall effect speed (based on visual effect for each pre-defined effect)
  * @param 		red color of effect from 0 to 255
  * @param 		green color of effect from 0 to 255
  * @param 		blue color of effect from 0 to 255
  *
  * @return
  *      -ESP_OK 	On success
  *      -ESP_FAIL 	Generic code indicating failure
  *
  **/
//TODO: insert restart_effect as a input parameter
esp_err_t led_strip_set_effect(struct led_strip_t *led_strip, effect_type_t effect_type, void *effect_arg)
{

	struct led_strip_effect_t led_strip_effect;
	esp_err_t ret = ESP_OK;
	led_strip_effect.led_strip = led_strip;
	led_strip_effect.effect_type = effect_type;
	led_strip_effect.effect_args = effect_arg;
	led_strip_effect.restart_effect = true;

	if( led_strip_effect_task_handle == NULL )
	{
		if( (ret = led_strip_init_effect_handler(led_strip, effect_type, effect_arg)) != ESP_OK)
		{
			ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
			return ret;
		}
	}else
	{
		if( xQueueSend( effect_queue_handle, ( void * ) &led_strip_effect, ( TickType_t ) 0 ) != pdPASS )
		{
			ret = ESP_FAIL;
			ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
			return ret;
		}
	}

	return ret;
}

/**
  * @brief     	Get current effect being showed
  *
  * @param 		led_strip_effect pointer to LED effect context
  *
  * @return
  *      -a led_strip_effect_t structure 0-valued if effect handler is not being executed
  *      -led_strip_effect structure with current data
  *
  **/
struct led_strip_effect_t led_strip_get_effect(struct led_strip_t *led_strip_to_check)
{
	struct led_strip_effect_t null_led_strip_effect;
	if(led_strip_to_check == curent_led_strip_effect.led_strip && led_strip_effect_task_handle != NULL)
	{
		return curent_led_strip_effect;
	}else
	{
		memset(&null_led_strip_effect, 0, sizeof(struct led_strip_effect_t));
		return null_led_strip_effect;
	}
}

/**
  * @brief     	Stop and Delete the task to handle LED strip effects and clears the LED strip
  *
  * @param 		pointer to led_strip strucutre
  *
  * @return
  *      -ESP_OK 	On success
  *      -ESP_ERR_NOT_FOUND		If effect queue or task handler was not found
  *
  **/
esp_err_t led_strip_delete_effect_handler(struct led_strip_t *led_strip)
{
	esp_err_t ret = ESP_OK;

	if(led_strip_effect_task_handle != NULL)
	{
		vTaskDelete(led_strip_effect_task_handle);
		led_strip_effect_task_handle = NULL;
	}else
	{
		ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
		ret = ESP_ERR_NOT_FOUND;
	}

	/* Check if effect queue was not previously created */
	if(effect_queue_handle != NULL)
	{
		vQueueDelete(effect_queue_handle);
		effect_queue_handle = NULL;
	}else /* If effect queue was not previously created, indicates an error */
	{
		ESP_LOGE(TAG, "Error in %s, line %d: %s in function %s", __FILE__, __LINE__, esp_err_to_name(ret), __func__);
		ret = ESP_ERR_NOT_FOUND;
	}
	vTaskDelay(LED_STRIP_REFRESH_PERIOD_MS / portTICK_PERIOD_MS);
	led_strip_clear(led_strip);

	return ret;
}
