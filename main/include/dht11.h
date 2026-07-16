// dht11.h
// DHT11 digital temperature/humidity sensor driver

#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Data Structure
// ============================================================
typedef struct {
    float temperature;   // Celsius
    float humidity;      // Percentage
    uint8_t checksum_ok; // 1 if checksum passed
} dht11_data_t;

// ============================================================
// Function Prototypes
// ============================================================

// Initialize DHT11 on a GPIO pin
void dht11_init(int gpio_pin);

// Read temperature and humidity (blocking, ~20ms)
int dht11_read(dht11_data_t *data);

// Check if DHT11 is present
int dht11_present(void);

void dht11_power_up(void);

#endif // DHT11_H