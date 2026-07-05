// sensor_read.h
// Sensor reading subsystem - combines ADC DMA + DHT11

#ifndef SENSOR_READ_H
#define SENSOR_READ_H

#include <stdint.h>
#include "project_defs.h"
#include "esp_adc/adc_continuous.h"
#include "driver/gpio.h"

// ============================================================
// Function Prototypes
// ============================================================

// Initialize all sensors
void sensor_init(void);

// Get the latest reading for a sensor
sensor_reading_t* sensor_get_reading(uint8_t sensor_id);

// Get all sensor readings
sensor_reading_t* sensor_get_all_readings(void);

// Convert raw ADC to sensor value
float sensor_convert_value(uint8_t sensor_id, uint16_t raw_adc);

// Update sensor configuration (reload from NVS)
void sensor_reload_config(void);

// Force a sensor poll (for calibration/testing)
int sensor_poll_now(uint8_t sensor_id);

// Enable/disable a sensor
void sensor_set_enabled(uint8_t sensor_id, uint8_t enabled);

// Get sensor name
const char* sensor_get_name(uint8_t sensor_id);

/**
 * @brief Get GPIO pin number for a specific ADC channel using ESP-IDF function
 * @param unit ADC unit (ADC_UNIT_1 or ADC_UNIT_2)
 * @param channel ADC channel (ADC_CHANNEL_0 to ADC_CHANNEL_7)
 * @return GPIO pin number or -1 if invalid
 */
int adc_channel_to_gpio(adc_unit_t unit, adc_channel_t channel);

/**
 * @brief Get JSON string of all ADC channels and their GPIO pins
 * @return Static JSON string with channel to GPIO mapping
 */
const char* adc_get_pin_mapping_json(void);

/**
 * @brief Get sensor-specific GPIO pin info using ESP-IDF function
 * @param sensor_name Name of the sensor (e.g., "pH", "ORP", "TDS", "TEMP")
 * @return String with GPIO info (e.g., "GPIO32")
 */
const char* sensor_get_gpio_info(const char *sensor_name);

/**
 * @brief Get ADC channel for a specific GPIO pin (reverse mapping)
 * @param gpio_num GPIO pin number
 * @return ADC channel or -1 if not found
 */
int gpio_to_adc_channel(int gpio_num);

#endif // SENSOR_READ_H