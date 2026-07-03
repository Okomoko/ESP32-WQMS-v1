// dht11.c
// DHT11 driver implementation

#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h" 

#include "dht11.h"
#include "log_levels.h"
#include "logger.h"

// ============================================================
// Static Variables
// ============================================================
static int dht11_pin = 13;
static int dht11_initialized = 0;

// ============================================================
// Internal Functions
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

static uint8_t dht11_wait_for_level(uint8_t level, uint32_t timeout_us) {
    uint32_t start = esp_timer_get_time();
    while (dht11_read_pin() != level) {
        if ((esp_timer_get_time() - start) > timeout_us) {
            return 0;  // Timeout
        }
    }
    return 1;  // Success
}

static uint8_t dht11_read_byte(void) {
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        // Wait for high (start of bit)
        if (!dht11_wait_for_level(1, 100)) {
            return 0;
        }
        uint32_t start = esp_timer_get_time();
        // Wait for low (end of bit)
        if (!dht11_wait_for_level(0, 100)) {
            return 0;
        }
        uint32_t elapsed = esp_timer_get_time() - start;
        // If high lasted > 50us, it's a '1'
        value <<= 1;
        if (elapsed > 50) {
            value |= 1;
        }
    }
    return value;
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
    
    // Clear data
    memset(data, 0, sizeof(dht11_data_t));
    
    // 1. Start signal: pull low for 18ms, then high for 40us
    dht11_set_output();
    dht11_write(0);
    dht11_delay_us(18000);
    dht11_write(1);
    dht11_delay_us(40);
    dht11_set_input();
    
    // 2. Wait for DHT11 response
    if (!dht11_wait_for_level(0, 80)) {
        SENSOR_LOG_D("DHT11: No response (low)");
        return -1;
    }
    if (!dht11_wait_for_level(1, 80)) {
        SENSOR_LOG_D("DHT11: No response (high)");
        return -1;
    }
    
    // 3. Read data (5 bytes: humidity, temp, checksum)
    uint8_t bytes[5] = {0};
    for (int i = 0; i < 5; i++) {
        bytes[i] = dht11_read_byte();
        if (bytes[i] == 0 && i > 0) {
            // Could be valid zero, but check previous bytes
        }
    }
    
    // 4. Verify checksum
    uint8_t checksum = bytes[0] + bytes[1] + bytes[2] + bytes[3];
    if (checksum != bytes[4]) {
        SENSOR_LOG_D("DHT11: Checksum error (calc=%d, got=%d)", checksum, bytes[4]);
        data->checksum_ok = 0;
        return -2;
    }
    data->checksum_ok = 1;
    
    // 5. Extract values
    data->humidity = bytes[0];
    data->temperature = bytes[2];
    
    // Check for negative temperature (MSB of temp)
    if (bytes[2] & 0x80) {
        data->temperature = -((bytes[2] & 0x7F) + bytes[3] * 0.1);
    } else {
        data->temperature = bytes[2] + bytes[3] * 0.1;
    }
    
    // Handle humidity decimal
    data->humidity = bytes[0] + bytes[1] * 0.1;
    
    // 6. Wait for bus release
    dht11_set_output();
    dht11_write(1);
    
    return 0;  // Success
}

int dht11_present(void) {
    if (!dht11_initialized) return 0;
    
    dht11_data_t data;
    int result = dht11_read(&data);
    return (result == 0);
}
