// ============================================================
// sensor_calib.c - Sensor Calibration Implementation
// ============================================================

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "sensor_calib.h"
#include "sensor_read.h"
#include "nvs_config.h"
#include "system_config.h"
#include "project_defs.h"
#include "log_levels.h"

// ============================================================
// Static Variables
// ============================================================
static cal_session_t cal_session = {0};
static bool cal_active = false;

// ============================================================
// NVS Keys
// ============================================================
// NVS_NAMESPACE is already defined in nvs_config.h or elsewhere
// We'll use the same namespace for consistency
#define NVS_KEY_FACTOR_PREFIX "factor_"

// ============================================================
// Helper: Convert ADC to Voltage
// ============================================================
static float adc_to_voltage(float raw_adc) {
    // Assuming 12-bit ADC (0-4095) with 3.3V reference
    return (raw_adc / 4095.0f) * 3.3f;
}

// ============================================================
// Start Calibration
// ============================================================
int cal_start(int sensor_id) {
    if (sensor_id < 0 || sensor_id >= ANALOGUE_SENSOR_COUNT) {
        SENSOR_LOG_E("Invalid sensor ID: %d", sensor_id);
        return -1;
    }
    
    // Cancel any existing session
    if (cal_active) {
        cal_cancel();
    }
    
    memset(&cal_session, 0, sizeof(cal_session));
    cal_session.sensor_id = sensor_id;
    cal_session.sample_count = 0;
    cal_session.start_time = esp_timer_get_time() / 1000;
    cal_session.active = true;
    cal_active = true;
    
    SENSOR_LOG_I("Calibration started for sensor %d", sensor_id);
    return 0;
}

// ============================================================
// Add Sample
// ============================================================
int cal_add_sample(float known_value) {
    if (!cal_active) {
        SENSOR_LOG_E("No active calibration session");
        return -1;
    }
    
    if (cal_session.sample_count >= CAL_SAMPLES_MAX) {
        SENSOR_LOG_E("Maximum samples reached (%d)", CAL_SAMPLES_MAX);
        return -1;
    }
    
    // Get current sensor reading
    sensor_reading_t *readings = sensor_get_all_readings();
    if (!readings) {
        SENSOR_LOG_E("Failed to get sensor reading");
        return -1;
    }
    
    int idx = cal_session.sample_count;
    cal_session.samples[idx].raw_adc = readings[cal_session.sensor_id].raw_adc;
    cal_session.samples[idx].voltage = adc_to_voltage(readings[cal_session.sensor_id].raw_adc);
    cal_session.samples[idx].known_value = known_value;
    cal_session.sample_count++;
    
    SENSOR_LOG_I("Sample %d: ADC=%.1f, Voltage=%.3fV, Known=%.2f",
             cal_session.sample_count,
             cal_session.samples[idx].raw_adc,
             cal_session.samples[idx].voltage,
             cal_session.samples[idx].known_value);
    
    return 0;
}

// ============================================================
// Calculate Factor (Linear Regression)
// ============================================================
float cal_calculate_factor(void) {
    if (!cal_active) {
        SENSOR_LOG_E("No active calibration session");
        return -1;
    }
    
    if (cal_session.sample_count < CAL_MIN_SAMPLES) {
        SENSOR_LOG_W("Need at least %d samples (got %d)", CAL_MIN_SAMPLES, cal_session.sample_count);
        return -1;
    }

    float sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
    int n = cal_session.sample_count;
    
    for (int i = 0; i < n; i++) {
        float x = cal_session.samples[i].voltage;
        float y = cal_session.samples[i].known_value;
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_xx += x * x;
    }
    
    float factor = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
    
    if (factor <= 0 || !isfinite(factor)) {
        SENSOR_LOG_E("Invalid factor: %.4f", factor);
        return 0;
    }
    
    SENSOR_LOG_I("Calibration factor: %.4f", factor);
    return factor;
}

// ============================================================
// Apply Calibration
// ============================================================
int cal_apply(void) {
    if (!cal_active) {
        SENSOR_LOG_E("No active calibration session");
        return -1;
    }
    
    if (cal_session.sample_count < CAL_MIN_SAMPLES) {
        SENSOR_LOG_E("Need at least %d samples (got %d)", 
                 CAL_MIN_SAMPLES, cal_session.sample_count);
        return -1;
    }
    
    float factor = cal_calculate_factor();
    if (factor <= 0) {
        SENSOR_LOG_E("Invalid calibration factor: %.4f", factor);
        return -1;
    }
    
    // Save factor to NVS
    int result = cal_save_factor(cal_session.sensor_id, factor);
    if (result != 0) {
        SENSOR_LOG_E("Failed to save calibration factor");
        return -1;
    }
    
    // Load all sensor configs
    sensor_config_t configs[TOTAL_SENSOR_COUNT];
    nvs_load_sensor_config(configs, TOTAL_SENSOR_COUNT);
    
    // Update the specific sensor's calibration factor
    configs[cal_session.sensor_id].calibration_factor = (int)(factor * 100);  // Store as scaled integer
    nvs_save_sensor_config(configs, TOTAL_SENSOR_COUNT);

    // Reload sensor config
    sensor_reload_config();

    SENSOR_LOG_I("Calibration applied: sensor %d, factor %.4f",
             cal_session.sensor_id, factor);
    
    // Clear session
    cal_active = false;
    memset(&cal_session, 0, sizeof(cal_session));
    
    return 0;
}

// ============================================================
// Cancel Calibration
// ============================================================
int cal_cancel(void) {
    if (!cal_active) {
        SENSOR_LOG_I("No active calibration to cancel");
        return -1;
    }
    
    cal_active = false;
    memset(&cal_session, 0, sizeof(cal_session));
    
    SENSOR_LOG_I("Calibration cancelled");
    return 0;
}

// ============================================================
// Get Session
// ============================================================
cal_session_t* cal_get_session(void) {
    if (!cal_active) {
        return NULL;
    }
    return &cal_session;
}

// ============================================================
// Check if Active
// ============================================================
bool cal_is_active(void) {
    return cal_active;
}

// ============================================================
// Get Sample Count
// ============================================================
int cal_get_sample_count(void) {
    if (!cal_active) {
        return 0;
    }
    return cal_session.sample_count;
}

// ============================================================
// Save Factor to NVS (using integer storage)
// ============================================================
int cal_save_factor(int sensor_id, float factor) {
    nvs_handle_t nvs_handle;
    // ============================================================
    // FIX: Use existing NVS_NAMESPACE (defined in nvs_config.h)
    // ============================================================
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        SENSOR_LOG_E("Failed to open NVS: %s", esp_err_to_name(err));
        return -1;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_FACTOR_PREFIX, sensor_id);
    
    // Store as integer (scaled by 1000 for 3 decimal precision)
    int32_t scaled_factor = (int32_t)(factor * 1000);
    err = nvs_set_i32(nvs_handle, key, scaled_factor);
    if (err != ESP_OK) {
        SENSOR_LOG_E("Failed to save factor: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return -1;
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        SENSOR_LOG_E("Failed to commit NVS: %s", esp_err_to_name(err));
        return -1;
    }
    
    SENSOR_LOG_I("Saved factor for sensor %d: %.4f", sensor_id, factor);
    return 0;
}

// ============================================================
// Load Factor from NVS (using integer storage)
// ============================================================
int cal_load_factor(int sensor_id, float *factor) {
    if (!factor) {
        return -1;
    }
    
    nvs_handle_t nvs_handle;
    // ============================================================
    // FIX: Use existing NVS_NAMESPACE (defined in nvs_config.h)
    // ============================================================
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        SENSOR_LOG_E("Failed to open NVS: %s", esp_err_to_name(err));
        return -1;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_FACTOR_PREFIX, sensor_id);
    
    // Read as integer and convert back to float
    int32_t scaled_factor;
    err = nvs_get_i32(nvs_handle, key, &scaled_factor);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        SENSOR_LOG_D("No factor found for sensor %d", sensor_id);
        return -1;
    }
    
    *factor = scaled_factor / 1000.0f;
    SENSOR_LOG_D("Loaded factor for sensor %d: %.4f", sensor_id, *factor);
    return 0;
}

// ============================================================
// Reset Calibration Module
// ============================================================
void cal_reset(void) {
    cal_active = false;
    memset(&cal_session, 0, sizeof(cal_session));
    SENSOR_LOG_I("Calibration module reset");
}