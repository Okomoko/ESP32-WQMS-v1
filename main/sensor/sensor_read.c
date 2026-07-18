// sensor_read.c
// Sensor reading subsystem implementation

#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_adc/adc_continuous.h"
#include "driver/gpio.h"

#include "sensor_read.h"
#include "sensor_calib.h"
#include "sensor_history.h"
#include "adc_dma.h"
#include "dht11.h"
#include "nvs_config.h"
#include "watchdog.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"

// ============================================================
// Static Variables
// ============================================================
static sensor_reading_t readings[TOTAL_SENSOR_COUNT];
static sensor_config_t sensor_config[TOTAL_SENSOR_COUNT];
static SemaphoreHandle_t sensor_mutex = NULL;
static int sensor_initialized = 0;
static int sensor_init_called = 0;
static uint32_t sample_counter = 0;

// ADC channel to sensor mapping
//static const uint8_t adc_to_sensor[] = {0, 1, 2, 3, 4, 5, 6, 7};

// ============================================================
// Internal Functions
// ============================================================

// Poll all analog sensors (via ADC DMA)
static void poll_analog_sensors(void) {
    if (!wqms_adc_dma_data_ready()) {
        return;
    }
    
    for (int i = 0; i < ANALOGUE_SENSOR_COUNT; i++) {
//        int sensor_id = adc_to_sensor[i];
        int sensor_id = i;
//        SENSOR_LOG_D("Sensor %d, name %s, channel %d, pin %d", i, sensor_config[sensor_id].name, sensor_config[sensor_id].adc_channel, sensor_config[sensor_id].gpio_pin);
        if (!sensor_config[sensor_id].enabled) continue;
        
        uint16_t raw = wqms_adc_dma_get_raw(sensor_config[i].adc_channel);
        float value = sensor_convert_value(sensor_id, raw);
        
        if (sensor_mutex && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            readings[sensor_id].timestamp = time(NULL);
            readings[sensor_id].value = value;
            readings[sensor_id].raw_adc = raw;
            readings[sensor_id].status = SENSOR_STATUS_OK;
            readings[sensor_id].quality = 0;
            
            if (value < sensor_config[sensor_id].min_value ||
                value > sensor_config[sensor_id].max_value) {
                readings[sensor_id].status = SENSOR_STATUS_OUT_OF_RANGE;
                readings[sensor_id].quality = 2;
                SENSOR_LOG_W("Sensor %d (%s) out of range: %.2f", sensor_id, sensor_config[sensor_id].name, value);
            }
            xSemaphoreGive(sensor_mutex);
        }
    }
    wqms_adc_dma_clear_ready();
}

// Poll DHT11 digital sensor (skips if both disabled)
static void poll_dht11(void) {
    // Skip if both temp and humidity sensors are disabled
    if (!sensor_config[DHT11_SENSOR_TEMP].enabled && 
        !sensor_config[DHT11_SENSOR_HUMID].enabled) {
        return;
    }
    
    dht11_data_t data;
    if (dht11_read(&data) == 0) {
        // Temperature (sensor 8)
        if (sensor_config[DHT11_SENSOR_TEMP].enabled) {
            if (sensor_mutex && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                readings[DHT11_SENSOR_TEMP].timestamp = time(NULL);
                readings[DHT11_SENSOR_TEMP].value = data.temperature;
                readings[DHT11_SENSOR_TEMP].raw_adc = 0;
                readings[DHT11_SENSOR_TEMP].status = SENSOR_STATUS_OK;
                readings[DHT11_SENSOR_TEMP].quality = 0;
                xSemaphoreGive(sensor_mutex);
            }
        }
        
        // Humidity (sensor 9)
        if (sensor_config[DHT11_SENSOR_HUMID].enabled) {
            if (sensor_mutex && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                readings[DHT11_SENSOR_HUMID].timestamp = time(NULL);
                readings[DHT11_SENSOR_HUMID].value = data.humidity;
                readings[DHT11_SENSOR_HUMID].raw_adc = 0;
                readings[DHT11_SENSOR_HUMID].status = SENSOR_STATUS_OK;
                readings[DHT11_SENSOR_HUMID].quality = 0;
                xSemaphoreGive(sensor_mutex);
            }
        }
    } else {
        // DHT11 read failed - only log if enabled
        if (sensor_config[DHT11_SENSOR_TEMP].enabled) {
            SENSOR_LOG_W("DHT11 read failed (temp)");
            if (sensor_mutex && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                readings[DHT11_SENSOR_TEMP].status = SENSOR_STATUS_ERROR;
                xSemaphoreGive(sensor_mutex);
            }
        }
        if (sensor_config[DHT11_SENSOR_HUMID].enabled) {
            SENSOR_LOG_W("DHT11 read failed (humid)");
            if (sensor_mutex && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                readings[DHT11_SENSOR_HUMID].status = SENSOR_STATUS_ERROR;
                xSemaphoreGive(sensor_mutex);
            }
        }
    }
}

// ============================================================
// Sensor Polling Task
// ============================================================
static void sensor_poll_task(void *pvParameters) {
    SENSOR_LOG_D("Sensor polling task started");
    
    while (1) {
        uint32_t start_time = esp_timer_get_time() / 1000;
        
        poll_analog_sensors();
        poll_dht11();
        
        watchdog_heartbeat(WDT_MODULE_SENSOR);
        
        sample_counter++;
        if (sample_counter >= 60) {
            sample_counter = 0;
            sensor_history_add();
        }
        
        uint32_t interval = nvs_get_sample_interval();
        uint32_t elapsed = (esp_timer_get_time() / 1000) - start_time;
        if (elapsed < interval) {
            vTaskDelay(pdMS_TO_TICKS(interval - elapsed));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// ============================================================
// Public Functions
// ============================================================

void sensor_init(void) {
    if (sensor_init_called) {
        SENSOR_LOG_W("sensor_init() called twice - ignoring second call");
        return;
    }
    sensor_init_called = 1;
    
    sensor_mutex = xSemaphoreCreateMutex();
    if (!sensor_mutex) {
        SENSOR_LOG_E("Failed to create sensor mutex");
        return;
    }
    
    // Load sensor configuration from NVS
    nvs_load_sensor_config(sensor_config, TOTAL_SENSOR_COUNT);
    
    if (wqms_adc_dma_init() != 0) {
        SENSOR_LOG_E("ADC DMA init failed - sensors will not work");
    } else {
        wqms_adc_dma_start();
    }
    
    dht11_init(GPIO_DHT11);
    dht11_power_up();
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Test read
    dht11_data_t data;
    if (dht11_read(&data) == 0) {
        SENSOR_LOG_I("DHT11 ready: Temp=%.1f, Humid=%.1f", data.temperature, data.humidity);
    }

    sensor_history_init();
    
    xTaskCreate(sensor_poll_task, "sensor", STACK_SIZE_SENSOR, NULL, PRIORITY_SENSOR, NULL);
    
    sensor_initialized = 1;
    SENSOR_LOG_I("Sensor subsystem initialized");
}

sensor_reading_t* sensor_get_reading(uint8_t sensor_id) {
    if (sensor_id >= TOTAL_SENSOR_COUNT) return NULL;
    return &readings[sensor_id];
}

sensor_reading_t* sensor_get_all_readings(void) {
    return readings;
}

float sensor_convert_value(uint8_t sensor_id, uint16_t raw_adc) {
    float voltage = (raw_adc / 4095.0f) * 5.0f;
    float calibrated = voltage * (sensor_config[sensor_id].calibration_factor / 1000.0f);
    
    switch (sensor_id) {
        case 0:  // Sensor 0
//            return (3.5f - calibrated) * 4.0f + 0.0f;
        case 1:  // Sensor 1
//            return calibrated * 2.0f;
        case 2:  // Sensor 2
        case 3:  // Sensor 3
        case 4:  // Sensor 4
        case 5:  // Sensor 5
        case 6:  // Sensor 6
        case 7:  // Sensor 7
//            return calibrated * 200.0f;
        case 8:  // Sensor 8 (Temperature from DHT11)
        case 9:  // Sensor 9 (Humidity from DHT11)
            return calibrated;
        default:
            return calibrated;
    }
}

void sensor_reload_config(void) {
    nvs_load_sensor_config(sensor_config, TOTAL_SENSOR_COUNT);
    SENSOR_LOG_D("Sensor config reloaded");
}

int sensor_poll_now(uint8_t sensor_id) {
    if (sensor_id >= TOTAL_SENSOR_COUNT) return -1;
    SENSOR_LOG_D("sensor_poll_now: %d", sensor_id);
    return 0;
}

void sensor_set_enabled(uint8_t sensor_id, uint8_t enabled) {
    if (sensor_id >= TOTAL_SENSOR_COUNT) return;
    
    // Update static config array
    sensor_config[sensor_id].enabled = enabled;
    
    // Save to NVS
    nvs_save_sensor_config(sensor_config, TOTAL_SENSOR_COUNT);
    
    // Update the reading status immediately
    if (sensor_mutex && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (enabled) {
            readings[sensor_id].status = SENSOR_STATUS_OK;
        } else {
            readings[sensor_id].status = SENSOR_STATUS_DISABLED;
            readings[sensor_id].value = 0.0f;
        }
        xSemaphoreGive(sensor_mutex);
    }
    
    SENSOR_LOG_D("Sensor %d %s", sensor_id, enabled ? "enabled" : "disabled");
}

char* sensor_get_name(uint8_t sensor_id) {
    if (sensor_id >= TOTAL_SENSOR_COUNT) return "Unknown";
    return sensor_config[sensor_id].name;
}

uint16_t sensor_get_calibration(uint8_t sensor_id) {
    if (sensor_id >= TOTAL_SENSOR_COUNT) return -1;
    return sensor_config[sensor_id].calibration_factor;
}

uint8_t sensor_get_gpio(int sensor_id) {
    if (sensor_id >= TOTAL_SENSOR_COUNT) return -1;
    return sensor_config[sensor_id].gpio_pin;
}

uint8_t sensor_get_modbus(int sensor_id) {
    if (sensor_id >= TOTAL_SENSOR_COUNT) return -1;
    return sensor_config[sensor_id].modbus_register;

}
uint8_t sensor_get_min(int sensor_id) {
    if (sensor_id >= TOTAL_SENSOR_COUNT) return -1;
    return sensor_config[sensor_id].min_value;
}

uint8_t sensor_get_max(int sensor_id) {
    if (sensor_id >= TOTAL_SENSOR_COUNT) return -1;
    return sensor_config[sensor_id].max_value;
}

/**
 * @brief Get GPIO pin number for a specific ADC channel using ESP-IDF function
 * @param channel ADC channel (ADC_CHANNEL_0 to ADC_CHANNEL_7)
 * @param unit ADC unit (ADC_UNIT_1 or ADC_UNIT_2)
 * @return GPIO pin number or -1 if invalid
 */
int adc_channel_to_gpio(adc_unit_t unit, adc_channel_t channel) {
    int gpio_num = -1;
    esp_err_t err = adc_continuous_channel_to_io(unit, channel, &gpio_num);
    
    if (err != ESP_OK) {
        SENSOR_LOG_E("Failed to get GPIO for ADC unit %d channel %d: %s", 
                 unit, channel, esp_err_to_name(err));
        return -1;
    }
    
    return gpio_num;
}

/**
 * @brief Get JSON string of all ADC1 channels and their GPIO pins
 * @return Static JSON string with channel to GPIO mapping
 */
const char* adc_get_pin_mapping_json(void) {
    static char json_buffer[512];
    json_buffer[0] = '\0';
    
    strcat(json_buffer, "{");
    
    // ADC1 channels (0-7)
    bool first = true;
    for (int i = 0; i < 8; i++) {
        int gpio = adc_channel_to_gpio(ADC_UNIT_1, i);
        if (gpio > 0) {
            if (!first) strcat(json_buffer, ",");
            char entry[32];
            snprintf(entry, sizeof(entry), "\"ADC%d\":%d", i, gpio);
            strcat(json_buffer, entry);
            first = false;
        }
    }
    
    strcat(json_buffer, "}");
    return json_buffer;
}

/**
 * @brief Get sensor-specific GPIO pin info using the ESP-IDF function
 * @param sensor_name Name of the sensor (e.g., "pH", "ORP", "TDS", "TEMP")
 * @return String with GPIO info (e.g., "GPIO32")
 */
const char* sensor_get_gpio_info(const char *sensor_name) {
    static char info_buffer[32];
    info_buffer[0] = '\0';
    
    if (!sensor_name) return "N/A";
    
    // Map sensor names to ADC channels
    // Adjust this based on your actual sensor configuration
    adc_channel_t channel = -1;
    
    if (strcasecmp(sensor_name, "pH") == 0 || strstr(sensor_name, "pH") != NULL) {
        channel = ADC_CHANNEL_4;
    } else if (strcasecmp(sensor_name, "ORP") == 0 || strstr(sensor_name, "ORP") != NULL) {
        channel = ADC_CHANNEL_5;
    } else if (strcasecmp(sensor_name, "TDS") == 0 || strstr(sensor_name, "TDS") != NULL) {
        channel = ADC_CHANNEL_6;
    } else if (strcasecmp(sensor_name, "TEMP") == 0 || strstr(sensor_name, "TEMP") != NULL) {
        channel = ADC_CHANNEL_7;
    } else {
        // Try to parse as ADC number (e.g., "ADC4")
        if (strstr(sensor_name, "ADC") != NULL) {
            int adc_num = atoi(sensor_name + 3);
            if (adc_num >= 0 && adc_num <= 7) {
                channel = adc_num;
            }
        }
    }
    
    if (channel >= 0 && channel <= 7) {
        int gpio = adc_channel_to_gpio(ADC_UNIT_1, channel);
        if (gpio > 0) {
            snprintf(info_buffer, sizeof(info_buffer), "GPIO%d", gpio);
        } else {
            snprintf(info_buffer, sizeof(info_buffer), "N/A");
        }
    } else {
        snprintf(info_buffer, sizeof(info_buffer), "N/A");
    }
    
    return info_buffer;
}

/**
 * @brief Get ADC channel for a specific GPIO pin (reverse mapping)
 * @param gpio_num GPIO pin number
 * @return ADC channel or -1 if not found
 */
int gpio_to_adc_channel(int gpio_num) {
    for (int i = 0; i < 8; i++) {
        int gpio = adc_channel_to_gpio(ADC_UNIT_1, i);
        if (gpio == gpio_num) {
            return i;
        }
    }
    return -1;
}