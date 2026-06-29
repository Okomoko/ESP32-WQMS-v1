// sensor_calib.c
// Sensor calibration implementation

#include <string.h>
#include <math.h>
#include "sensor_calib.h"
#include "sensor_read.h"
#include "nvs_config.h"
#include "log_levels.h"
#include "system_config.h"

// ============================================================
// Static Variables
// ============================================================
static cal_session_t session = {0};
static sensor_config_t sensor_config[SENSOR_COUNT];

// ============================================================
// Internal Functions
// ============================================================

static float calculate_slope(cal_sample_t *samples, int count) {
    if (count < 2) return 1.0f;
    
    if (count == 2) {
        // Linear interpolation
        float dx = samples[1].raw_adc - samples[0].raw_adc;
        if (fabs(dx) < 0.001f) return 1.0f;
        return (samples[1].known_value - samples[0].known_value) / dx;
    }
    
    if (count >= 3) {
        // Linear regression
        float sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
        for (int i = 0; i < count; i++) {
            sum_x += samples[i].raw_adc;
            sum_y += samples[i].known_value;
            sum_xy += samples[i].raw_adc * samples[i].known_value;
            sum_xx += samples[i].raw_adc * samples[i].raw_adc;
        }
        float n = count;
        float denom = n * sum_xx - sum_x * sum_x;
        if (fabs(denom) < 0.001f) return 1.0f;
        return (n * sum_xy - sum_x * sum_y) / denom;
    }
    
    return 1.0f;
}

// ============================================================
// Public Functions
// ============================================================

int cal_start(uint8_t sensor_id) {
    if (sensor_id >= SENSOR_COUNT) {
        LOG_E("Invalid sensor ID: %d", sensor_id);
        return -1;
    }
    
    // Reset session
    memset(&session, 0, sizeof(session));
    session.sensor_id = sensor_id;
    session.active = 1;
    
    // Load sensor config
    nvs_load_sensor_config(sensor_config, SENSOR_COUNT);
    
    LOG_I("Calibration started for sensor %d (%s)", 
          sensor_id, sensor_config[sensor_id].name);
    return 0;
}

int cal_add_sample(float known_value) {
    if (!session.active) {
        LOG_E("No active calibration session");
        return -1;
    }
    
    if (session.sample_count >= 3) {
        LOG_E("Maximum samples reached (3)");
        return -1;
    }
    
    // Get current reading
    sensor_reading_t *reading = sensor_get_reading(session.sensor_id);
    if (!reading || reading->status != SENSOR_STATUS_OK) {
        LOG_E("Sensor reading not available");
        return -1;
    }
    
    // Store sample
    cal_sample_t *sample = &session.samples[session.sample_count];
    sample->known_value = known_value;
    sample->raw_adc = reading->raw_adc;
    sample->voltage = (reading->raw_adc / 4095.0f) * 5.0f;
    session.sample_count++;
    
    LOG_I("Calibration sample %d added: known=%.2f, ADC=%d, V=%.3f",
          session.sample_count, known_value, reading->raw_adc, sample->voltage);
    return 0;
}

int cal_apply(void) {
    if (!session.active) {
        LOG_E("No active calibration session");
        return -1;
    }
    
    if (session.sample_count < 2) {
        LOG_E("Need at least 2 samples for calibration");
        return -1;
    }
    
    // Calculate calibration factor
    float slope = calculate_slope(session.samples, session.sample_count);
    
    // Convert to integer (×1000)
    uint16_t cal_factor = (uint16_t)(slope * 1000.0f);
    if (cal_factor < 1) cal_factor = 1;
    if (cal_factor > 10000) cal_factor = 10000;
    
    // Save to NVS
    nvs_load_sensor_config(sensor_config, SENSOR_COUNT);
    sensor_config[session.sensor_id].calibration_factor = cal_factor;
    nvs_save_sensor_config(sensor_config, SENSOR_COUNT);
    
    // Reload config
    sensor_reload_config();
    
    LOG_I("Calibration applied: sensor %d, factor=%.3f (×1000=%d)",
          session.sensor_id, slope, cal_factor);
    
    // Clear session
    session.active = 0;
    return 0;
}

void cal_cancel(void) {
    if (session.active) {
        LOG_I("Calibration cancelled for sensor %d", session.sensor_id);
        session.active = 0;
    }
}

cal_session_t* cal_get_session(void) {
    return &session;
}

float cal_calculate_factor(void) {
    if (!session.active || session.sample_count < 2) {
        return 1.0f;
    }
    return calculate_slope(session.samples, session.sample_count);
}

void cal_set_factor(uint8_t sensor_id, float factor) {
    if (sensor_id >= SENSOR_COUNT) return;
    
    uint16_t cal_factor = (uint16_t)(factor * 1000.0f);
    if (cal_factor < 1) cal_factor = 1;
    if (cal_factor > 10000) cal_factor = 10000;
    
    nvs_load_sensor_config(sensor_config, SENSOR_COUNT);
    sensor_config[sensor_id].calibration_factor = cal_factor;
    nvs_save_sensor_config(sensor_config, SENSOR_COUNT);
    
    sensor_reload_config();
    LOG_I("Manual calibration set: sensor %d, factor=%.3f", sensor_id, factor);
}
