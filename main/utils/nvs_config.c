// nvs_config.c
// NVS configuration management implementation

#include <string.h>
#include <stdio.h>
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_adc/adc_continuous.h"
#include <time.h>
#include <sys/time.h>

//#include "wifi_manager.h"
#include "nvs_config.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"

#define CONFIG_VERSION 0

// ============================================================
// Default RELAY Definitions
// ============================================================
const char *default_relay_names[] = { "Pump 1", "Valve 1", "Pump 2", "Valve 2", "Pump 3", "Valve 3", "Pump 4", "Valve 4", "Pump 5", "Valve 5"};
const uint8_t default_relay_gpios[] = {GPIO_RELAY1, GPIO_RELAY2, GPIO_RELAY3, GPIO_RELAY4, GPIO_RELAY5, GPIO_RELAY6, GPIO_RELAY7, GPIO_RELAY8, GPIO_RELAY9, GPIO_RELAY10};
const uint16_t default_relay_modbus[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9};

// ============================================================
// Default SENSOR Definitions
// ============================================================
const char *default_sensor_names[] = {"Sensor 1", "Sensor 2", "Sensor 3", "Sensor 4", "Sensor 5", "Sensor 6", "Sensor 7", "Sensor 8"};
const uint8_t default_sensor_gpios[] = {GPIO_SENSOR1, GPIO_SENSOR2, GPIO_SENSOR3, GPIO_SENSOR4, GPIO_SENSOR5, GPIO_SENSOR6, GPIO_DHT11, GPIO_DHT11};
const uint16_t default_sensor_modbus[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7};

// Static array to be filled at runtime
static modbus_map_entry_t default_modbus_map[MODBUS_MAP_ENTRY_COUNT];

// ============================================================
// Default Sensor Configurations
// ============================================================
static void load_default_sensor_configs(sensor_config_t *config, int count) {
    adc_unit_t adc_unit;
    adc_channel_t adc_channel;
    esp_err_t ret;
    for (int i = 0; i < count && i < TOTAL_SENSOR_COUNT; i++) {
        strncpy(config[i].name, default_sensor_names[i], sizeof(config[i].name) - 1);
        config[i].name[sizeof(config[i].name) - 1] = '\0';
        config[i].enabled = 0;
        config[i].gpio_pin = default_sensor_gpios[i];
        if (config[i].gpio_pin != GPIO_DHT11) {
            ret = adc_continuous_io_to_channel(config[i].gpio_pin, &adc_unit, &adc_channel);
            if (ret == ESP_OK) {
                config[i].adc_channel = adc_channel;
                WQMS_LOG_V("Sensor %d, GPIO %d, ADC Unit %d Channel %d", i, config[i].gpio_pin, adc_unit, adc_channel);
            } else {
                WQMS_LOG_E("Sensor %d, GPIO %d, ADC Channels cannot be optained, error code is %d.", i, config[i].gpio_pin, ret);
            }
        } else {
            config[i].adc_channel = 255;
            WQMS_LOG_V("DHT11 ADC Channel is set to 255");
        }
        config[i].modbus_register = default_sensor_modbus[i];
        config[i].calibration_factor = 1;
        config[i].safe_min = 0;
        config[i].safe_max = 100;
    }
}

// ============================================================
// Default Relay Configurations
// ============================================================
static void load_default_relay_configs(relay_config_t *config, int count) {
    for (int i = 0; i < count && i < RELAY_COUNT; i++) {
        strncpy(config[i].name, default_relay_names[i], sizeof(config[i].name) - 1);
        config[i].name[sizeof(config[i].name) - 1] = '\0';
        config[i].enabled = 0;
        config[i].gpio_pin = default_relay_gpios[i];
        config[i].modbus_register = default_relay_modbus[i];
        config[i].activity_duration = RELAY_DEFAULT_DURATION_MS;
        config[i].off_delay = RELAY_DEFAULT_OFFDELAY_MS;
        config[i].control_device = 0;
    }
}

// ============================================================
// Internal Helper Functions
// ============================================================
esp_err_t wqms_nvs_get_str(const char *key, char *buffer, size_t max_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    
    size_t len = max_len;
    err = nvs_get_str(handle, key, buffer, &len);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    return err;
}

esp_err_t wqms_nvs_set_str(const char *key, const char *value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

uint32_t wqms_nvs_get_u32(const char *key, uint32_t default_val) {
    nvs_handle_t handle;
    uint32_t value = default_val;
    
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u32(handle, key, &value);
        nvs_close(handle);
    }
    return value;
}

esp_err_t wqms_nvs_set_u32(const char *key, uint32_t value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_u32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

// ============================================================
// Static Variables for Supabase Config
// ============================================================
static char supabase_sensor_url[256] = "";
static char supabase_log_url[256] = "";
static char supabase_api_key[128] = "";
static uint32_t supabase_upload_interval = SUPABASE_UPLOAD_INTERVAL;

// ============================================================
// Public Functions - Supabase Config
// ============================================================

const char* nvs_get_supabase_sensor_url(void) { return supabase_sensor_url; }
const char* nvs_get_supabase_log_url(void) { return supabase_log_url; }
const char* nvs_get_supabase_api_key(void) { return supabase_api_key; }
uint32_t nvs_get_supabase_upload_interval(void) { return supabase_upload_interval; }

void nvs_set_supabase_sensor_url(const char *url) {
    if (url) {
        strncpy(supabase_sensor_url, url, sizeof(supabase_sensor_url) - 1);
        supabase_sensor_url[sizeof(supabase_sensor_url) - 1] = '\0';
        wqms_nvs_set_str(NVS_KEY_SUPABASE_SENSOR_URL, supabase_sensor_url);
        WQMS_LOG_I("Supabase Sensor URL updated to: %s", supabase_sensor_url);
    }
}

void nvs_set_supabase_log_url(const char *url) {
    if (url) {
        strncpy(supabase_log_url, url, sizeof(supabase_log_url) - 1);
        supabase_log_url[sizeof(supabase_log_url) - 1] = '\0';
        wqms_nvs_set_str(NVS_KEY_SUPABASE_LOG_URL, supabase_log_url);
        WQMS_LOG_I("Supabase Log URL updated to: %s", supabase_log_url);
    }
}

void nvs_set_supabase_api_key(const char *key) {
    if (key) {
        strncpy(supabase_api_key, key, sizeof(supabase_api_key) - 1);
        supabase_api_key[sizeof(supabase_api_key) - 1] = '\0';
        wqms_nvs_set_str(NVS_KEY_SUPABASE_API_KEY, supabase_api_key);
        WQMS_LOG_I("Supabase API Key updated");
    }
}

void nvs_set_supabase_upload_interval(uint32_t sec) {
    if (sec < 10) sec = 10;
    if (sec > 60) sec = 60;
    supabase_upload_interval = sec;
    wqms_nvs_set_u32(NVS_KEY_SUPABASE_UPLOAD_INTERVAL, supabase_upload_interval);
    WQMS_LOG_I("Supabase upload interval updated to: %lu sec", supabase_upload_interval);
}

bool nvs_supabase_is_configured(void) {
    return (strlen(supabase_sensor_url) > 0 &&
            strlen(supabase_log_url) > 0 &&
            strlen(supabase_api_key) > 0);
}

// ============================================================
// Static Variables for System Config
// ============================================================
static char system_name_prefix[12] = "WQMS-System";
static char system_name[32] = "WQMS-System";
static char system_location[32] = "Unknown";
static char timezone[16] = "EET-3";
static uint32_t sample_interval_ms = 1000;
static uint32_t automation_interval_sec = DEFAULT_AUTOMATION_INTERVAL_SEC;

// ============================================================
// Public Functions - System Config
// ============================================================

void nvs_config_init(void) {
    WQMS_LOG_I("NVS config initialized");
}

void nvs_config_load(void) {
    uint8_t version = 0;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, NVS_KEY_CONFIG_VERSION, &version);
        nvs_close(handle);
        WQMS_LOG_I("Config version from NVS: %d", version);
    }
    
    if (version != CONFIG_VERSION) {
        WQMS_LOG_W("Config version mismatch (got %d, expected %d), loading defaults", version, CONFIG_VERSION);
        
        sensor_config_t default_sensors[TOTAL_SENSOR_COUNT];
        relay_config_t default_relays[RELAY_COUNT];
        load_default_sensor_configs(default_sensors, TOTAL_SENSOR_COUNT);
        load_default_relay_configs(default_relays, RELAY_COUNT);
        nvs_save_sensor_config(default_sensors, TOTAL_SENSOR_COUNT);
        nvs_save_relay_config(default_relays, RELAY_COUNT);
        
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_u8(handle, NVS_KEY_CONFIG_VERSION, CONFIG_VERSION);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }
    
    char temp_name[32] = {0};
    esp_err_t err = wqms_nvs_get_str(NVS_KEY_SYSTEM_NAME, temp_name, sizeof(temp_name));
    if (err == ESP_OK && strlen(temp_name) > 0) {
        strcpy(system_name, temp_name);
        WQMS_LOG_I("Loaded system_name from NVS: '%s'", system_name);
    } else {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(system_name, sizeof(system_name), "%s-%02X%02X%02X%02X%02X%02X",
                 system_name_prefix, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        WQMS_LOG_W("System name not found in NVS (err=%d), using default: '%s'", err, system_name);
    }
    
    wqms_nvs_get_str(NVS_KEY_SYSTEM_LOCATION, system_location, sizeof(system_location));
    wqms_nvs_get_str(NVS_KEY_TIMEZONE, timezone, sizeof(timezone));
    
    sample_interval_ms = wqms_nvs_get_u32(NVS_KEY_SAMPLE_INTERVAL, 1000);
    automation_interval_sec = wqms_nvs_get_u32(NVS_KEY_AUTOMATION_INT, DEFAULT_AUTOMATION_INTERVAL_SEC);

    wqms_nvs_get_str(NVS_KEY_SUPABASE_SENSOR_URL, supabase_sensor_url, sizeof(supabase_sensor_url));
    wqms_nvs_get_str(NVS_KEY_SUPABASE_LOG_URL, supabase_log_url, sizeof(supabase_log_url));
    wqms_nvs_get_str(NVS_KEY_SUPABASE_API_KEY, supabase_api_key, sizeof(supabase_api_key));
    supabase_upload_interval = wqms_nvs_get_u32(NVS_KEY_SUPABASE_UPLOAD_INTERVAL, 10);
    if (supabase_upload_interval < 10) supabase_upload_interval = 10;
    if (supabase_upload_interval > 60) supabase_upload_interval = 60;
    
    WQMS_LOG_I("Config loaded: system='%s', location='%s', interval=%lu ms", 
               system_name, system_location, sample_interval_ms);
}

void nvs_config_save(void) {
    wqms_nvs_set_str(NVS_KEY_SYSTEM_NAME, system_name);
    wqms_nvs_set_str(NVS_KEY_SYSTEM_LOCATION, system_location);
    wqms_nvs_set_str(NVS_KEY_TIMEZONE, timezone);
    wqms_nvs_set_u32(NVS_KEY_SAMPLE_INTERVAL, sample_interval_ms);
    wqms_nvs_set_u32(NVS_KEY_AUTOMATION_INT, automation_interval_sec);
    // Save Supabase config
    wqms_nvs_set_str(NVS_KEY_SUPABASE_SENSOR_URL, supabase_sensor_url);
    wqms_nvs_set_str(NVS_KEY_SUPABASE_LOG_URL, supabase_log_url);
    wqms_nvs_set_str(NVS_KEY_SUPABASE_API_KEY, supabase_api_key);
    wqms_nvs_set_u32(NVS_KEY_SUPABASE_UPLOAD_INTERVAL, supabase_upload_interval);
    WQMS_LOG_I("Config saved");
}

const char* nvs_get_system_name(void) { return system_name; }
const char* nvs_get_system_location(void) { return system_location; }
const char* nvs_get_timezone(void) { return timezone; }
uint32_t nvs_get_sample_interval(void) { return sample_interval_ms; }
uint32_t nvs_get_automation_interval(void) { return automation_interval_sec; }

void nvs_set_system_name(const char *name) {
    if (name) {
        strncpy(system_name, name, sizeof(system_name) - 1);
        system_name[sizeof(system_name) - 1] = '\0';
        esp_err_t err = wqms_nvs_set_str(NVS_KEY_SYSTEM_NAME, system_name);
        WQMS_LOG_I("System name saved to NVS: err=%d, name='%s'", err, system_name);
    }
}

void nvs_set_system_location(const char *loc) {
    if (loc) {
        strncpy(system_location, loc, sizeof(system_location) - 1);
        system_location[sizeof(system_location) - 1] = '\0';
        esp_err_t err = wqms_nvs_set_str(NVS_KEY_SYSTEM_LOCATION, system_location);
        WQMS_LOG_I("System location saved to NVS: err=%d, location='%s'", err, system_location);
    }
}

void nvs_set_timezone(const char *tz) {
    if (tz) {
        strncpy(timezone, tz, sizeof(timezone) - 1);
        timezone[sizeof(timezone) - 1] = '\0';
        wqms_nvs_set_str(NVS_KEY_TIMEZONE, timezone);
        WQMS_LOG_I("Timezone updated to: %s", timezone);
    }
}

void nvs_set_sample_interval(uint32_t ms) {
    sample_interval_ms = ms;
    wqms_nvs_set_u32(NVS_KEY_SAMPLE_INTERVAL, sample_interval_ms);
    WQMS_LOG_I("Sample interval updated to: %lu ms", sample_interval_ms);
}

void nvs_set_automation_interval(uint32_t sec) {
    automation_interval_sec = sec;
    wqms_nvs_set_u32(NVS_KEY_AUTOMATION_INT, automation_interval_sec);
    WQMS_LOG_I("Automation interval updated to: %lu sec", automation_interval_sec);
}

// ============================================================
// Public Functions - MODBUS Map
// ============================================================

void nvs_load_modbus_map(modbus_map_entry_t *map, int count) {
    // First load defaults
    sensor_config_t sconfigs[TOTAL_SENSOR_COUNT];
    nvs_load_sensor_config(sconfigs, TOTAL_SENSOR_COUNT);
    int entry_index = 0;
    
    // Add sensors
    for (int i = 0; i < TOTAL_SENSOR_COUNT; i++) {
        default_modbus_map[entry_index].address = sconfigs[i].modbus_register;
        strcpy(default_modbus_map[entry_index].type, "Sensor");
        strcpy(default_modbus_map[entry_index].description, sconfigs[i].name);
        strcpy(default_modbus_map[entry_index].access, "RO");
        entry_index++;
    }
    
    relay_config_t rconfigs[RELAY_COUNT];
    nvs_load_relay_config(rconfigs, RELAY_COUNT);
    // Add relays
    for (int i = 0; i < RELAY_COUNT; i++) {
        default_modbus_map[entry_index].address = rconfigs[i].modbus_register;
        strcpy(default_modbus_map[entry_index].type, "Relay");
        strcpy(default_modbus_map[entry_index].description, rconfigs[i].name);
        strcpy(default_modbus_map[entry_index].access, "RW");
        entry_index++;
    }

    memcpy(map, default_modbus_map, count * sizeof(modbus_map_entry_t));
    
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    
    size_t len = count * sizeof(modbus_map_entry_t);
    if (nvs_get_blob(handle, NVS_KEY_MODBUS_MAP, map, &len) == ESP_OK) {
        WQMS_LOG_D("Loaded MODBUS map from NVS: %d entries", count);
    } else {
        WQMS_LOG_D("MODBUS map not found in NVS, using defaults");
    }
    
    nvs_close(handle);
}

void nvs_save_modbus_map(modbus_map_entry_t *map, int count) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        WQMS_LOG_E("Failed to open NVS for MODBUS map save");
        return;
    }
    
    nvs_set_blob(handle, NVS_KEY_MODBUS_MAP, map, count * sizeof(modbus_map_entry_t));
    nvs_commit(handle);
    nvs_close(handle);
    WQMS_LOG_I("MODBUS map saved to NVS: %d entries", count);
}

// ============================================================
// Public Functions - Sensor Configuration
// ============================================================

void nvs_load_sensor_config(sensor_config_t *config, int count) {
    load_default_sensor_configs(config, count);
    
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    for (int i = 0; i < count && i < TOTAL_SENSOR_COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "%s%d_name", NVS_KEY_SENSOR_PREFIX, i);
        size_t len = sizeof(config[i].name);
        nvs_get_str(handle, key, config[i].name, &len);
        
        snprintf(key, sizeof(key), "%s%d_en", NVS_KEY_SENSOR_PREFIX, i);
        uint8_t val = 0;
        if (nvs_get_u8(handle, key, &val) == ESP_OK) {
            config[i].enabled = val;
        }

        snprintf(key, sizeof(key), "%s%d_cal", NVS_KEY_SENSOR_PREFIX, i);

        float cal = 1000.0f;
		uint32_t raw_bits;
        if (nvs_get_u32(handle, key, &raw_bits) == ESP_OK) {
			memcpy(&cal, &raw_bits, sizeof(raw_bits)); 
            config[i].calibration_factor = cal;
        }

        snprintf(key, sizeof(key), "%s%d_mb", NVS_KEY_SENSOR_PREFIX, i);
        uint16_t mb = default_sensor_modbus[i];
        if (nvs_get_u16(handle, key, &mb) == ESP_OK) {
            config[i].modbus_register = mb;
        }

        snprintf(key, sizeof(key), "%s%d_unit", NVS_KEY_SENSOR_PREFIX, i);
        uint8_t unit = 0;
        if (nvs_get_u8(handle, key, &unit) == ESP_OK) {
            config[i].unit = unit;
        }

        snprintf(key, sizeof(key), "%s%d_safe_min", NVS_KEY_SENSOR_PREFIX, i);
        float safe_min = 0;
        if (nvs_get_u32(handle, key, &raw_bits) == ESP_OK) {
			memcpy(&safe_min, &raw_bits, sizeof(raw_bits)); 
            config[i].safe_min = safe_min;
        }

        snprintf(key, sizeof(key), "%s%d_safe_max", NVS_KEY_SENSOR_PREFIX, i);
        float safe_max = 0;
        if (nvs_get_u32(handle, key, &raw_bits) == ESP_OK) {
			memcpy(&safe_max, &raw_bits, sizeof(raw_bits)); 
            config[i].safe_max = safe_max;
        }
    }
    
    nvs_close(handle);
    WQMS_LOG_V("Loaded %d sensor configs from NVS", count);
}

void nvs_save_sensor_config(sensor_config_t *config, int count) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        WQMS_LOG_E("Failed to open NVS for sensor config save");
        return;
    }

    for (int i = 0; i < count && i < TOTAL_SENSOR_COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "%s%d_name", NVS_KEY_SENSOR_PREFIX, i);
        nvs_set_str(handle, key, config[i].name);
        
        snprintf(key, sizeof(key), "%s%d_en", NVS_KEY_SENSOR_PREFIX, i);
        nvs_set_u8(handle, key, config[i].enabled);

		uint32_t raw_bits;
		memcpy(&raw_bits, &config[i].calibration_factor, sizeof(config[i].calibration_factor));
        snprintf(key, sizeof(key), "%s%d_cal", NVS_KEY_SENSOR_PREFIX, i);
        nvs_set_u32(handle, key, raw_bits);

        snprintf(key, sizeof(key), "%s%d_mb", NVS_KEY_SENSOR_PREFIX, i);
        nvs_set_u16(handle, key, config[i].modbus_register);

        snprintf(key, sizeof(key), "%s%d_unit", NVS_KEY_SENSOR_PREFIX, i);
        nvs_set_u8(handle, key, config[i].unit);

		memcpy(&raw_bits, &config[i].safe_min, sizeof(config[i].safe_min));
        snprintf(key, sizeof(key), "%s%d_safe_min", NVS_KEY_SENSOR_PREFIX, i);
        nvs_set_u32(handle, key, raw_bits);

		memcpy(&raw_bits, &config[i].safe_max, sizeof(config[i].safe_max));
        snprintf(key, sizeof(key), "%s%d_safe_max", NVS_KEY_SENSOR_PREFIX, i);
        nvs_set_u32(handle, key, raw_bits);
    }
    
    nvs_commit(handle);
    nvs_close(handle);
    WQMS_LOG_D("Saved %d sensor configs to NVS", count);
}

// ============================================================
// Public Functions - Relay Configuration
// ============================================================

void nvs_load_relay_config(relay_config_t *config, int count) {
    load_default_relay_configs(config, count);
    
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    for (int i = 0; i < count && i < RELAY_COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "%s%d_name", NVS_KEY_RELAY_PREFIX, i);
        size_t len = sizeof(config[i].name);
        nvs_get_str(handle, key, config[i].name, &len);

        snprintf(key, sizeof(key), "%s%d_en", NVS_KEY_RELAY_PREFIX, i);
        uint8_t val = 0;
        if (nvs_get_u8(handle, key, &val) == ESP_OK) {
            config[i].enabled = val;
        }

        snprintf(key, sizeof(key), "%s%d_dur", NVS_KEY_RELAY_PREFIX, i);
        uint16_t dur = RELAY_DEFAULT_DURATION_MS;
        if (nvs_get_u16(handle, key, &dur) == ESP_OK) {
            config[i].activity_duration = dur;
        }

        snprintf(key, sizeof(key), "%s%d_off", NVS_KEY_RELAY_PREFIX, i);
        uint16_t off = RELAY_DEFAULT_OFFDELAY_MS;
        if (nvs_get_u16(handle, key, &off) == ESP_OK) {
            config[i].off_delay = off;
        }

        snprintf(key, sizeof(key), "%s%d_mb", NVS_KEY_RELAY_PREFIX, i);
        uint16_t mb = default_relay_modbus[i];
        if (nvs_get_u16(handle, key, &mb) == ESP_OK) {
            config[i].modbus_register = mb;
        }

        snprintf(key, sizeof(key), "%s%d_cd", NVS_KEY_RELAY_PREFIX, i);
        uint8_t cd = 0;
        if (nvs_get_u8(handle, key, &cd) == ESP_OK) {
            config[i].control_device = cd;
        }
    }
    nvs_close(handle);
    WQMS_LOG_D("Loaded %d relay configs from NVS", count);
}

void nvs_save_relay_config(relay_config_t *config, int count) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        WQMS_LOG_E("Failed to open NVS for relay config save");
        return;
    }

    for (int i = 0; i < count && i < RELAY_COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "%s%d_name", NVS_KEY_RELAY_PREFIX, i);
        nvs_set_str(handle, key, config[i].name);
        
        snprintf(key, sizeof(key), "%s%d_en", NVS_KEY_RELAY_PREFIX, i);
        nvs_set_u8(handle, key, config[i].enabled);
        
        snprintf(key, sizeof(key), "%s%d_dur", NVS_KEY_RELAY_PREFIX, i);
        nvs_set_u16(handle, key, config[i].activity_duration);
        
        snprintf(key, sizeof(key), "%s%d_off", NVS_KEY_RELAY_PREFIX, i);
        nvs_set_u16(handle, key, config[i].off_delay);

        snprintf(key, sizeof(key), "%s%d_mb", NVS_KEY_RELAY_PREFIX, i);
        nvs_set_u16(handle, key, config[i].modbus_register);

        snprintf(key, sizeof(key), "%s%d_cd", NVS_KEY_RELAY_PREFIX, i);
        nvs_set_u8(handle, key, config[i].control_device);
    }
    
    nvs_commit(handle);
    nvs_close(handle);
    WQMS_LOG_D("Saved %d relay configs to NVS", count);
}

// ============================================================
// Public Functions - Date & Time
// ============================================================

esp_err_t nvs_save_datetime(void) {
    time_t now;
    struct tm timeinfo;
    char datetime_str[64];
    char timezone_str[16];
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Get current timezone
    const char* tz = nvs_get_timezone();
    strncpy(timezone_str, tz, sizeof(timezone_str) - 1);
    timezone_str[sizeof(timezone_str) - 1] = '\0';
    
    // Format: "2026-07-30 14:30:00 EET-3"
    if (strftime(datetime_str, sizeof(datetime_str), "%Y-%m-%d %H:%M:%S", &timeinfo) == 0) {
        return ESP_FAIL;
    }
    
    // Append timezone
    char full_datetime[80];
    snprintf(full_datetime, sizeof(full_datetime), "%s %s", datetime_str, timezone_str);
    
    return wqms_nvs_set_str("datetime", full_datetime);
}

esp_err_t nvs_get_datetime(void) {
    char full_datetime[80];
    char datetime_str[32];
    char timezone_str[16];
    esp_err_t err;
    struct tm timeinfo;
    
    err = wqms_nvs_get_str("datetime", full_datetime, sizeof(full_datetime));
    if (err != ESP_OK) {
        return err;
    }
    
    // Parse datetime and timezone: "2026-07-30 14:30:00 EET-3"
    int parsed = sscanf(full_datetime, "%31[^'] %15s", datetime_str, timezone_str);
    if (parsed != 2) {
        return ESP_FAIL;
    }
    
    if (strptime(datetime_str, "%Y-%m-%d %H:%M:%S", &timeinfo) == NULL) {
        return ESP_FAIL;
    }
    
    // Set timezone
    nvs_set_timezone(timezone_str);
    
    // Set system time from NVS
    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    err = settimeofday(&tv, NULL);
    
    return err;
}

void nvs_factory_reset(void) {
    WQMS_LOG_W("Factory reset requested - erasing NVS");
    nvs_flash_erase();
    WQMS_LOG_I("NVS erased, system will restart");
    esp_restart();
}

