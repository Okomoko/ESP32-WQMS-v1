// dht11.c - Modified version with NO logging inside critical section

#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dht11.h"
#include "log_levels.h"
#include "logger.h"

static const char *TAG = "DHT11";

// ============================================================
// Static Variables
// ============================================================
static int dht11_pin = 13;
static int dht11_initialized = 0;
static portMUX_TYPE dht11_mux = portMUX_INITIALIZER_UNLOCKED;

// ✅ Error tracking for logging outside critical section
static int last_error_code = 0;
static int last_error_bit = -1;

// ============================================================
// Internal Functions - NO LOGGING INSIDE
// ============================================================
static void dht11_set_output(void) {
    gpio_set_direction(dht11_pin, GPIO_MODE_OUTPUT);
}

static void dht11_set_input(void) {
    gpio_set_direction(dht11_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(dht11_pin, GPIO_PULLUP_ONLY);
}

static void dht11_write(uint8_t value) {
    gpio_set_level(dht11_pin, value);
}

static uint8_t dht11_read_pin(void) {
    return gpio_get_level(dht11_pin);
}

static void dht11_delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

// ✅ NO LOGGING - just return success/failure
static uint8_t dht11_wait_for_level(uint8_t level, uint32_t timeout_us) {
    uint32_t start = esp_timer_get_time();
    while (dht11_read_pin() != level) {
        if ((esp_timer_get_time() - start) > timeout_us) {
            return 0;  // Timeout - no logging
        }
    }
    return 1;
}

// ✅ NO LOGGING - just return 0 on error
static uint8_t dht11_read_byte(void) {
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        if (!dht11_wait_for_level(1, 100)) {
            return 0;  // Error - no logging
        }
        uint32_t start = esp_timer_get_time();
        if (!dht11_wait_for_level(0, 100)) {
            return 0;  // Error - no logging
        }
        uint32_t elapsed = esp_timer_get_time() - start;
        value <<= 1;
        if (elapsed > 40) {
            value |= 1;
        }
    }
    return value;
}

// ✅ NO LOGGING - just return error codes
static int dht11_read_raw(dht11_data_t *data) {
    memset(data, 0, sizeof(dht11_data_t));
    
    dht11_set_output();
    dht11_write(0);
    dht11_delay_us(18000);
    dht11_write(1);
    dht11_delay_us(40);
    dht11_set_input();
    
    if (!dht11_wait_for_level(0, 80)) {
        return -1;  // No response low
    }
    if (!dht11_wait_for_level(1, 80)) {
        return -2;  // No response high
    }
    
    uint8_t bytes[5] = {0};
    for (int i = 0; i < 5; i++) {
        bytes[i] = dht11_read_byte();
    }
    
    uint8_t checksum = bytes[0] + bytes[1] + bytes[2] + bytes[3];
    if (checksum != bytes[4]) {
        return -3;  // Checksum error
    }
    data->checksum_ok = 1;
    
    data->humidity = bytes[0];
    data->temperature = bytes[2];
    
    if (bytes[2] & 0x80) {
        data->temperature = -((bytes[2] & 0x7F) + bytes[3] * 0.1);
    } else {
        data->temperature = bytes[2] + bytes[3] * 0.1;
    }
    data->humidity = bytes[0] + bytes[1] * 0.1;
    
    dht11_set_output();
    dht11_write(1);

    return 0;  // Success
}

// ============================================================
// Public Functions
// ============================================================

void dht11_init(int gpio_pin) {
    dht11_pin = gpio_pin;
    gpio_set_direction(dht11_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(dht11_pin, 1);
    dht11_initialized = 1;
    SENSOR_LOG_D("DHT11 initialized on GPIO %d", gpio_pin);
}

int dht11_read(dht11_data_t *data) {
    if (!dht11_initialized) {
        SENSOR_LOG_E("DHT11 not initialized");
        return -1;
    }
    
    if (!data) {
        SENSOR_LOG_E("DHT11: NULL data pointer");
        return -1;
    }
    
    int ret = -1;
    
    // ✅ Enter critical section - NO LOGGING INSIDE
    taskENTER_CRITICAL(&dht11_mux);
    
    for (int attempt = 0; attempt < 3; attempt++) {
        ret = dht11_read_raw(data);
        if (ret == 0) {
            break;  // Success
        }
        if (attempt < 2) {
            taskEXIT_CRITICAL(&dht11_mux);
            vTaskDelay(pdMS_TO_TICKS(100));
            taskENTER_CRITICAL(&dht11_mux);
        }
    }
    
    // ✅ Exit critical section BEFORE any logging
    taskEXIT_CRITICAL(&dht11_mux);
    
    // ✅ All logging happens OUTSIDE critical section
    if (ret == 0) {
        SENSOR_LOG_D("DHT11: Temp=%.1f°C, Humid=%.1f%%", 
                     data->temperature, data->humidity);
    } else {
        // Log the error outside critical section
        switch(ret) {
            case -1:
                SENSOR_LOG_E("DHT11: No response (low)");
                break;
            case -2:
                SENSOR_LOG_E("DHT11: No response (high)");
                break;
            case -3:
                SENSOR_LOG_E("DHT11: Checksum error");
                break;
            default:
                SENSOR_LOG_W("DHT11 read failed (attempts=3)");
                break;
        }
    }
    
    return ret;
}

int dht11_present(void) {
    if (!dht11_initialized) return 0;
    
    dht11_data_t data;
    int result = dht11_read(&data);
    return (result == 0);
}