// adc_dma.c
// Simplified ADC driver - single-shot mode (reliable)

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "adc_dma.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"

// ============================================================
// Constants
// ============================================================
#define ADC_OVERSAMPLING        8       // Number of samples to average

// ============================================================
// Static Variables
// ============================================================
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static int latest_mv[ADC_CHANNEL_COUNT] = {0};
static uint16_t latest_raw[ADC_CHANNEL_COUNT] = {0};
static SemaphoreHandle_t adc_mutex = NULL;
static int adc_initialized = 0;

// ============================================================
// Internal Functions
// ============================================================

// Read a single ADC channel with oversampling
static int read_adc_channel(int channel) {
    if (!adc_handle || !adc_initialized) {
        return -1;
    }
    
    int sum = 0;
    int valid_samples = 0;
    
    for (int i = 0; i < ADC_OVERSAMPLING; i++) {
        int raw;
        esp_err_t err = adc_oneshot_read(adc_handle, channel, &raw);
        if (err == ESP_OK) {
            sum += raw;
            valid_samples++;
        } else {
            // If read fails, try reconfiguring
            SENSOR_LOG_D("ADC read failed on channel %d: %s", channel, esp_err_to_name(err));
        }
    }
    
    if (valid_samples == 0) {
        return -1;
    }
    
    return sum / valid_samples;
}

// ============================================================
// Public Functions
// ============================================================

int wqms_adc_dma_init(void) {
    if (adc_initialized) {
        SENSOR_LOG_W("ADC already initialized");
        return 0;
    }
    
    // Create mutex
    adc_mutex = xSemaphoreCreateMutex();
    if (!adc_mutex) {
        SENSOR_LOG_E("Failed to create ADC mutex");
        return -1;
    }
    
    // Configure ADC unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    
    esp_err_t err = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (err != ESP_OK) {
        SENSOR_LOG_E("ADC oneshot init failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(adc_mutex);
        adc_mutex = NULL;
        return -1;
    }
    
    // Configure each channel
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    
    for (int i = 0; i < ADC_CHANNEL_COUNT; i++) {
        err = adc_oneshot_config_channel(adc_handle, i, &config);
        if (err != ESP_OK) {
            SENSOR_LOG_W("Failed to config ADC channel %d: %s", i, esp_err_to_name(err));
        }
    }
    
    // Set up calibration
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle);
    if (err != ESP_OK) {
        SENSOR_LOG_W("ADC calibration not available, using raw values");
        adc_cali_handle = NULL;
    } else {
        SENSOR_LOG_I("ADC calibration initialized");
    }
    
    adc_initialized = 1;
    SENSOR_LOG_I("ADC initialized (single-shot mode, 8 channels)");
    return 0;
}

int wqms_adc_dma_start(void) {
    if (!adc_initialized) {
        SENSOR_LOG_E("ADC not initialized");
        return -1;
    }
    SENSOR_LOG_I("ADC ready (single-shot mode)");
    return 0;
}

int wqms_adc_dma_stop(void) {
    return 0;
}

uint16_t wqms_adc_dma_get_raw(int channel) {
    if (channel < 0 || channel >= ADC_CHANNEL_COUNT) {
        return 0;
    }
    if (!adc_initialized || !adc_handle) {
        return 0;
    }
    
    uint16_t value = 0;
    if (adc_mutex && xSemaphoreTake(adc_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int raw = read_adc_channel(channel);
        if (raw >= 0) {
            value = (uint16_t)raw;
            latest_raw[channel] = value;
        }
        xSemaphoreGive(adc_mutex);
    }
    return value;
}

int wqms_adc_dma_get_voltage_mv(int channel) {
    if (channel < 0 || channel >= ADC_CHANNEL_COUNT) {
        return 0;
    }
    if (!adc_initialized || !adc_handle) {
        return 0;
    }
    
    int value = 0;
    if (adc_mutex && xSemaphoreTake(adc_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int raw = read_adc_channel(channel);
        if (raw >= 0) {
            if (adc_cali_handle) {
                adc_cali_raw_to_voltage(adc_cali_handle, raw, &value);
            } else {
                // Approximate voltage from raw value (0-4095 → 0-3.3V)
                value = (raw * 3300) / 4095;
            }
            latest_mv[channel] = value;
        }
        xSemaphoreGive(adc_mutex);
    }
    return value;
}

float wqms_adc_dma_get_voltage(int channel) {
    return wqms_adc_dma_get_voltage_mv(channel) / 1000.0f;
}

int wqms_adc_dma_data_ready(void) {
    return adc_initialized ? 1 : 0;
}

void wqms_adc_dma_clear_ready(void) {
    // Nothing to clear in single-shot mode
}