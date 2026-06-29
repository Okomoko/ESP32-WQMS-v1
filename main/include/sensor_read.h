// sensor_read.h
// Sensor reading subsystem - combines ADC DMA + DHT11

#ifndef SENSOR_READ_H
#define SENSOR_READ_H

#include <stdint.h>
#include "project_defs.h"

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

#endif // SENSOR_READ_H