// dht11.c

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
#include "system_config.h"

// ============================================================
// Static Variables
// ============================================================
static int dht11_initialized = 0;
static int dht11_powered = 0;
static portMUX_TYPE dht11_mux = portMUX_INITIALIZER_UNLOCKED;

// ============================================================
// Internal Functions - NO LOGGING INSIDE
// ============================================================
static void dht11_set_output(void) {
    gpio_set_direction(GPIO_DHT11, GPIO_MODE_OUTPUT);
}

static void dht11_set_input(void) {
    gpio_set_direction(GPIO_DHT11, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_DHT11, GPIO_PULLUP_ONLY);
}

static void dht11_write(uint8_t value) {
    gpio_set_level(GPIO_DHT11, value);
}

static uint8_t dht11_read_pin(void) {
    return gpio_get_level(GPIO_DHT11);
}

static void dht11_delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

static uint8_t dht11_wait_for_level(uint8_t level, uint32_t timeout_us) {
    uint32_t start = esp_timer_get_time();
    uint32_t elapsed;
    
    do {
        if (dht11_read_pin() == level) {
            return 1;
        }
        elapsed = esp_timer_get_time() - start;
    } while (elapsed < timeout_us);
    
    return 0;
}

// Returns error code instead of ambiguous 0
#define DHT11_BYTE_OK      0
#define DHT11_BYTE_TIMEOUT 1

static int dht11_read_byte(uint8_t *value) {
    uint8_t result = 0;
    
    for (int i = 0; i < 8; i++) {
        // Wait for start of bit (high)
        if (!dht11_wait_for_level(1, 100)) {
            return DHT11_BYTE_TIMEOUT;
        }
        
        uint32_t start = esp_timer_get_time();
        
        // Wait for end of bit (low)
        if (!dht11_wait_for_level(0, 100)) {
            return DHT11_BYTE_TIMEOUT;
        }
        
        uint32_t elapsed = esp_timer_get_time() - start;
        result <<= 1;
        if (elapsed > 40) {
            result |= 1;
        }
    }
    
    *value = result;
    return DHT11_BYTE_OK;
}

// Improved with better error detection and reset
static int dht11_read_raw(dht11_data_t *data) {
    uint8_t bytes[5] = {0};
    int ret;
    
    // Clear data
    memset(data, 0, sizeof(dht11_data_t));
    
    // Ensure pin is high before starting (reset condition)
    dht11_set_output();
    dht11_write(1);
    dht11_delay_us(500);  // Increased wait time
    
    // Start signal
    dht11_write(0);
    dht11_delay_us(20000);  // Minimum 18ms, DHT11 needs at least 18ms, 20ms would be safer
    dht11_write(1);
    dht11_delay_us(40);    // 20-40us
    dht11_set_input();
    
    // Wait for sensor response
    if (!dht11_wait_for_level(0, 100)) {
        return -1;  // No response low
    }
    if (!dht11_wait_for_level(1, 100)) {
        return -2;  // No response high
    }
    
    // Read 5 bytes with timeout for each byte
    for (int i = 0; i < 5; i++) {
        ret = dht11_read_byte(&bytes[i]);
        if (ret != DHT11_BYTE_OK) {
            // Reset the bus
            dht11_set_output();
            dht11_write(1);
            return -4;  // Bit timeout error
        }
    }
    
    // Verify checksum
    uint8_t checksum = bytes[0] + bytes[1] + bytes[2] + bytes[3];
    if (checksum != bytes[4]) {
        // Reset the bus
        dht11_set_output();
        dht11_write(1);
        return -3;  // Checksum error
    }
    
    data->checksum_ok = 1;
    data->humidity = bytes[0] + bytes[1] * 0.1;
    
    // Temperature (handle negative values)
    if (bytes[2] & 0x80) {
        data->temperature = -((bytes[2] & 0x7F) + bytes[3] * 0.1);
    } else {
        data->temperature = bytes[2] + bytes[3] * 0.1;
    }
    
    // Reset bus
    dht11_set_output();
    dht11_write(1);
    
    return 0;  // Success
}

// ============================================================
// Public Functions
// ============================================================

void dht11_init(int gpio_pin) {
    
    // Configure pin with pull-up enabled
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    // Initial high state
    gpio_set_level(gpio_pin, 1);
    
    dht11_powered = 0;
    dht11_initialized = 1;
    
    SENSOR_LOG_D("DHT11 initialized on GPIO %d", gpio_pin);
}

// New function to power up the sensor properly
void dht11_power_up(void) {
    if (!dht11_initialized) return;
    
    dht11_set_output();
    dht11_write(1);
    vTaskDelay(pdMS_TO_TICKS(5000));  // Wait 5 seconds for stabilization
    dht11_powered = 1;
    SENSOR_LOG_D("DHT11 powered up and stable");
}

int dht11_read(dht11_data_t *data) {
    if (!dht11_initialized) {
        SENSOR_LOG_E("DHT11 not initialized");
        return -1;
    }
    
    if (!dht11_powered) {
        // First read - power up and stabilize
        dht11_power_up();
    }
    
    if (!data) {
        SENSOR_LOG_E("DHT11: NULL data pointer");
        return -1;
    }
    
    int ret = -1;
    int attempts = 3;
    
    // Use shorter critical section - only protect the actual read
    taskENTER_CRITICAL(&dht11_mux);
    ret = dht11_read_raw(data);
    taskEXIT_CRITICAL(&dht11_mux);

    // Retry with delay if failed
    for (int attempt = 1; attempt < attempts && ret != 0; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(300));  // DHT11 needs 200ms between reads, but I prefer to wait some more.
        taskENTER_CRITICAL(&dht11_mux);
        ret = dht11_read_raw(data);
        taskEXIT_CRITICAL(&dht11_mux);
    }

    // All logging outside critical section
    if (ret == 0) {
        SENSOR_LOG_V("DHT11: Temp=%.1f°C, Humid=%.1f%%", data->temperature, data->humidity);
    } else {
        switch(ret) {
            case -1:
                SENSOR_LOG_E("DHT11: No response low signal");
                break;
            case -2:
                SENSOR_LOG_E("DHT11: No response high signal");
                break;
            case -3:
                SENSOR_LOG_E("DHT11: Checksum error");
                break;
            case -4:
                SENSOR_LOG_E("DHT11: Bit read timeout");
                break;
            default:
                SENSOR_LOG_W("DHT11 read failed (attempts=%d)", attempts);
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

// New function to reset the sensor
void dht11_reset(void) {
    if (!dht11_initialized) return;
    
    taskENTER_CRITICAL(&dht11_mux);
    dht11_set_output();
    dht11_write(1);
    dht11_delay_us(1000);
    taskEXIT_CRITICAL(&dht11_mux);
    
    dht11_powered = 0;
    vTaskDelay(pdMS_TO_TICKS(100));
    SENSOR_LOG_D("DHT11 reset");
}