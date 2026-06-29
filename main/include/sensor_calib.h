// sensor_calib.h
// Sensor calibration - 2-3 sample wizard

#ifndef SENSOR_CALIB_H
#define SENSOR_CALIB_H

#include <stdint.h>
#include "project_defs.h"

// ============================================================
// Data Structures
// ============================================================
typedef struct {
    float known_value;   // Operator-entered reference
    uint16_t raw_adc;    // ADC reading at time of sample
    float voltage;       // Calculated voltage
} cal_sample_t;

typedef struct {
    uint8_t sensor_id;
    cal_sample_t samples[3];
    uint8_t sample_count;
    uint8_t active;      // 1 if calibration session active
} cal_session_t;

// ============================================================
// Function Prototypes
// ============================================================

// Start a calibration session
int cal_start(uint8_t sensor_id);

// Add a sample to the current session
int cal_add_sample(float known_value);

// Apply calibration (calculate factor and save)
int cal_apply(void);

// Cancel current session
void cal_cancel(void);

// Get current session data
cal_session_t* cal_get_session(void);

// Get the calculated calibration factor (without applying)
float cal_calculate_factor(void);

// Manually set calibration factor
void cal_set_factor(uint8_t sensor_id, float factor);

#endif // SENSOR_CALIB_H