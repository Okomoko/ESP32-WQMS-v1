// nvs_config.c
// NVS configuration management implementation

#include <string.h>
#include <stdio.h>
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"

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
const uint8_t default_relay_gpios[] = {4, 5, 18, 19, 21, 22, 23, 25, 26, 27};
const uint16_t default_relay_modbus[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9};

// ============================================================
// Default SENSOR Definitions
// ============================================================
const char *default_sensor_names[] = {"Sensor 1", "Sensor 2", "Sensor 3", "Sensor 4", "Sensor 5", "Sensor 6", "Sensor 7", "Sensor 8"};
const uint8_t default_sensor_gpios[] = {36, 39, 34, 35, 32, 33, GPIO_DHT11, GPIO_DHT11};
const uint8_t default_sensor_adc[] = {0, 3, 4, 5, 6, 7, 255, 255};
const uint16_t default_sensor_modbus[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7};
const float default_sensor_min[] = {0, 0, 0, 0, 0, 0, -10, 0};
const float default_sensor_max[] = {1000, 1000, 1000, 1000, 1000, 1000, 60, 100};

// Static array to be filled at runtime
static modbus_map_entry_t default_modbus_map[MODBUS_MAP_ENTRY_COUNT];

// ============================================================
// Default Sensor Configurations
// ============================================================
static void load_default_sensor_configs(sensor_config_t *config, int count) {
    for (int i = 0; i < count && i < TOTAL_SENSOR_COUNT; i++) {
        strncpy(config[i].name, default_sensor_names[i], sizeof(config[i].name) - 1);
        config[i].name[sizeof(config[i].name) - 1] = '\0';
        config[i].enabled = 0;
        config[i].gpio_pin = default_sensor_gpios[i];
        config[i].adc_channel = default_sensor_adc[i];
        config[i].modbus_register = default_sensor_modbus[i];
        config[i].calibration_factor = 1000;
        config[i].min_value = default_sensor_min[i];
        config[i].max_value = default_sensor_max[i];
        config[i].unit = 0; //blank
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
// Static Variables for System Config
// ============================================================
static char system_name_prefix[12] = "WQMS-System";
static char system_name[32] = "WQMS-System";
static char system_location[32] = "Unknown";
static char timezone[16] = "EET-3";
static uint32_t sample_interval_ms = 1000;
static uint32_t modbus_interval_ms = 1000;
static char integration_url[128] = "https://api.example.com/water";
static uint32_t integration_interval_sec = DEFAULT_INTEGRATION_INTERVAL_SEC;
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
    modbus_interval_ms = wqms_nvs_get_u32(NVS_KEY_MODBUS_INTERVAL, 1000);
    integration_interval_sec = wqms_nvs_get_u32(NVS_KEY_INTEGRATION_INT, DEFAULT_INTEGRATION_INTERVAL_SEC);
    wqms_nvs_get_str(NVS_KEY_INTEGRATION_URL, integration_url, sizeof(integration_url));
    automation_interval_sec = wqms_nvs_get_u32(NVS_KEY_AUTOMATION_INT, DEFAULT_AUTOMATION_INTERVAL_SEC);
    
    WQMS_LOG_I("Config loaded: system='%s', location='%s', interval=%lu ms", 
               system_name, system_location, sample_interval_ms);
}

void nvs_config_save(void) {
    wqms_nvs_set_str(NVS_KEY_SYSTEM_NAME, system_name);
    wqms_nvs_set_str(NVS_KEY_SYSTEM_LOCATION, system_location);
    wqms_nvs_set_str(NVS_KEY_TIMEZONE, timezone);
    wqms_nvs_set_u32(NVS_KEY_SAMPLE_INTERVAL, sample_interval_ms);
    wqms_nvs_set_u32(NVS_KEY_MODBUS_INTERVAL, modbus_interval_ms);
    wqms_nvs_set_u32(NVS_KEY_INTEGRATION_INT, integration_interval_sec);
    wqms_nvs_set_str(NVS_KEY_INTEGRATION_URL, integration_url);
    wqms_nvs_set_u32(NVS_KEY_AUTOMATION_INT, automation_interval_sec);
    WQMS_LOG_I("Config saved");
}

const char* nvs_get_system_name(void) { return system_name; }
const char* nvs_get_system_location(void) { return system_location; }
const char* nvs_get_timezone(void) { return timezone; }
uint32_t nvs_get_sample_interval(void) { return sample_interval_ms; }
uint32_t nvs_get_modbus_interval(void) { return modbus_interval_ms; }
const char* nvs_get_integration_url(void) { return integration_url; }
uint32_t nvs_get_integration_interval(void) { return integration_interval_sec; }
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

void nvs_set_modbus_interval(uint32_t ms) {
    modbus_interval_ms = ms;
    wqms_nvs_set_u32(NVS_KEY_MODBUS_INTERVAL, modbus_interval_ms);
    WQMS_LOG_I("MODBUS interval updated to: %lu ms", modbus_interval_ms);
}

void nvs_set_integration_url(const char *url) {
    if (url) {
        strncpy(integration_url, url, sizeof(integration_url) - 1);
        integration_url[sizeof(integration_url) - 1] = '\0';
        wqms_nvs_set_str(NVS_KEY_INTEGRATION_URL, integration_url);
        WQMS_LOG_I("Integration URL updated to: %s", integration_url);
    }
}

void nvs_set_integration_interval(uint32_t sec) {
    integration_interval_sec = sec;
    wqms_nvs_set_u32(NVS_KEY_INTEGRATION_INT, integration_interval_sec);
    WQMS_LOG_I("Integration interval updated to: %lu sec", integration_interval_sec);
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
        uint16_t cal = 1000;
        if (nvs_get_u16(handle, key, &cal) == ESP_OK) {
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
    }
    
    nvs_close(handle);
    WQMS_LOG_D("Loaded %d sensor configs from NVS", count);
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
        
        snprintf(key, sizeof(key), "%s%d_cal", NVS_KEY_SENSOR_PREFIX, i);
        nvs_set_u16(handle, key, config[i].calibration_factor);

        snprintf(key, sizeof(key), "%s%d_mb", NVS_KEY_SENSOR_PREFIX, i);
        nvs_set_u16(handle, key, config[i].modbus_register);

        snprintf(key, sizeof(key), "%s%d_unit", NVS_KEY_SENSOR_PREFIX, i);
        nvs_set_u8(handle, key, config[i].unit);
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

    }
    
    nvs_commit(handle);
    nvs_close(handle);
    WQMS_LOG_D("Saved %d relay configs to NVS", count);
}

void nvs_factory_reset(void) {
    WQMS_LOG_W("Factory reset requested - erasing NVS");
    nvs_flash_erase();
    WQMS_LOG_I("NVS erased, system will restart");
    esp_restart();
}