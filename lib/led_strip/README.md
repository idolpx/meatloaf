# ESP32 Addressable LED Strip Library

## Description
This library can be used to drive addressable LED strips from the ESP32 using the RMT peripheral. This allows the RMT peripheral to handle all of the transmission while the processor is free to support other tasks.

The library currently uses double buffering to seperate the LED strip that's being currently displayed and the LED strip that is currently being updated. There are two buffers, 1 and 2, which contain the colors for the LED strip. When the driver is showing buffer 1, any calls to led_strip_set_pixel_color will update buffer 2. When a call to led_strip_show is made, it will switch out the buffers, so buffer 2 is currently being displayed and buffer 1 is the one that is written to when calls to led_strip_set_pixel_color are made.

## How to use
All functions for initializing and setting colors are located in led_strip.h. Right now the library supports:

Initialization of an led strip which initializes the RMT peripheral and starts a task for driving LEDs
```c
bool led_strip_init(struct led_strip_t *led_strip);
```

Setting individual led colors on an LED strip
```c
bool led_strip_set_pixel_color(struct led_strip_t *led_strip, uint32_t pixel_num, struct led_color_t *color);
bool led_strip_set_pixel_rgb(struct led_strip_t *led_strip, uint32_t pixel_num, uint8_t red, uint8_t green, uint8_t blue);
```

Updates the LED strip being shown. This switches the buffer currently being used to display the LED strips
```c
bool led_strip_show(struct led_strip_t *led_strip);
```

Get the color of a pixel on the strip **currently being shown**. For instance, if you want to update the color of the led strip based on the color that's currently being shown, use this function.
```c
bool led_strip_get_pixel_color(struct led_strip_t *led_strip, uint32_t pixel_num, struct led_color_t *color);
```

Clear the led strip
```c
bool led_strip_clear(struct led_strip_t *led_strip)
```

Below is simple configuration
```c
#define LED_STRIP_LENGTH 17U
#define LED_STRIP_RMT_INTR_NUM 19U

static struct led_color_t led_strip_buf_1[LED_STRIP_LENGTH];
static struct led_color_t led_strip_buf_2[LED_STRIP_LENGTH];

struct led_strip_t led_strip = {
    .rgb_led_type = RGB_LED_TYPE_WS2812,
    .rmt_channel = RMT_CHANNEL_1,
    .rmt_interrupt_num = LED_STRIP_RMT_INTR_NUM,
    .gpio = GPIO_NUM_21,
    .led_strip_buf_1 = led_strip_buf_1,
    .led_strip_buf_2 = led_strip_buf_2,
    .led_strip_length = LED_STRIP_LENGTH
};
led_strip.access_semaphore = xSemaphoreCreateBinary();

bool led_init_ok = led_strip_init(&led_strip);
```

Expect more examples in main.c in the near future for use cases excercising all functionality of the library.

## Limitations
1. Right now the library only supports the timing for WS2812/WS2812B LED strips. I believe most of the Adafruit Neopixel strips use these LED drivers.  The ability to add drivers for other LEDs like the SK6812 and WS2811 is implemented, but I don't have any to test on.
2. Only supports 30ms refresh period. Will add functionality to make this configurable in the future.

## Future Goals of Library (in somewhat prioritized order)
1. Add higher level functions for generating rainbows, bouncing effects, etc.
2. Somehow clean up led strip c file. This may involve seperating the waveform generation of each kind of LED.
3. Add support for "LED rooms". An LED room may contain one or more strips covering multiple walls and multiple corners. This would allow very cool LED shows for different setups, such as LEDs bouncing off walls, LEDs mirroring what the other strip is doing parallel or perpendicular to it, etc.
4. Add support for other LED strips (WS2811, SK6812, etc.)
5. Add ESP supported logging and ESP checks at the beginning and end of function
6. Add notes about memory limitations. Right now depending on how its done, there are two buffers that are either stack allocated or statically allocated and another buffer for the rmt items that's malloc'd on the heap.
