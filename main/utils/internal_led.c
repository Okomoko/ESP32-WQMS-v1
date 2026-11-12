#include "internal_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_levels.h"
#include "logger.h"
#include "log_rotate.h"
#include "led_strip.h"

// ESP32-S3 standard addressable onboard LED pin
#define BLINK_GPIO 48 
#define RGB_MAX 50 // Maximum intensity for rainbow
static led_strip_handle_t led_strip = NULL;
static bool is_initialized = false;

void Internal_LED_Init(void)
{
    if (is_initialized) {
        return;
    }

    /* General configuration for led_strip v3.x+ */
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // Proper field name for led_strip v3.x
        .led_model = LED_MODEL_WS2812,
    };

    /* RMT backend configuration */
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (err == ESP_OK) {
        is_initialized = true;
        led_strip_clear(led_strip);
        WQMS_LOG_I("Internal LED Module Initialized Successfully on GPIO %d", BLINK_GPIO);
    } else {
        WQMS_LOG_E("Failed to initialize internal LED: %s", esp_err_to_name(err));
    }
}

void Internal_LED_On_Explicit(rgb_color_t color)
{
    if (!is_initialized || !led_strip) return;
    
    led_strip_set_pixel(led_strip, 0, color.r, color.g, color.b);
    led_strip_refresh(led_strip);
}

void Internal_LED_Off(void)
{
    if (!is_initialized || !led_strip) return;

    led_strip_clear(led_strip);
}

void Internal_LED_Blink(int howmanytimes, rgb_color_t color)
{
    if (!is_initialized || !led_strip) return;

    for (int i = 0; i < howmanytimes; i++) {
        Internal_LED_On_Explicit(color);
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms On
        
        Internal_LED_Off();
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms Off
    }
}

void Internal_LED_Rainbow(int howmanytimes)
{
    rgb_color_t c;
    for (int i = 0; i < howmanytimes; i++) {

        // Phase 1: Red -> Yellow   (R=50, G: 0->50, B=0)
        for (int g = 0; g <= RGB_MAX; g++) {
            c.r = RGB_MAX; c.g = g; c.b = 0;
            Internal_LED_On_Explicit(c);
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Phase 2: Yellow -> Green -> Cyan  (R: 50->0, G=50, B: 0->50)
        //   R and B move together: R = 50-i, B = i, for i = 1..50
        for (int i = 1; i <= RGB_MAX; i++) {
            c.r = RGB_MAX - i; c.g = RGB_MAX; c.b = i;
            Internal_LED_On_Explicit(c);
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Phase 3: Cyan -> Blue  (R=0, G: 50->0, B=50)
        for (int g = RGB_MAX - 1; g >= 0; g--) {
            c.r = 0; c.g = g; c.b = RGB_MAX;
            Internal_LED_On_Explicit(c);
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Phase 4: Blue -> Violet -> Purple -> Magenta  (R: 0->50, G=0, B=50)
        //   R rises while B stays at max -> passes through violet/purple/magenta
        for (int r = 1; r <= RGB_MAX; r++) {
            c.r = r; c.g = 0; c.b = RGB_MAX;
            Internal_LED_On_Explicit(c);
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Phase 5: Magenta -> Red  (R=50, G=0, B: 50->0)
        //   B drops while R stays at max -> closes the loop back to red
//        for (int b = RGB_MAX - 1; b >= 0; b--) {
//            c.r = RGB_MAX; c.g = 0; c.b = b;
//            Internal_LED_On_Explicit(c);
//            vTaskDelay(pdMS_TO_TICKS(10));
//        }
    }
    Internal_LED_Off();
}