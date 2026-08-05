// nvs_config.h
// NVS configuration management - load/save system settings

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "project_defs.h"

// ============================================================
// NVS Namespace
// ============================================================
#define NVS_NAMESPACE "wqms"

// ============================================================
// NVS Keys
// ============================================================
#define NVS_KEY_SYSTEM_NAME              "sys_name"
#define NVS_KEY_SYSTEM_LOCATION          "sys_loc"
#define NVS_KEY_TIMEZONE                 "timezone"
#define NVS_KEY_SAMPLE_INTERVAL          "samp_int"
#define NVS_KEY_CONFIG_VERSION           "cfg_ver"
#define NVS_KEY_AUTOMATION_INT           "auto_int"
#define NVS_KEY_SUPABASE_PROJECT_URL     "project_url"
#define NVS_KEY_SUPABASE_BUCKET_NAME     "bucket_name"
#define NVS_KEY_SUPABASE_API_KEY         "api_key"
#define NVS_KEY_SUPABASE_UPLOAD_INTERVAL "upload_int"
#define NVS_KEY_SUPABASE_CERT            "sup_cert"
#define NVS_KEY_SUPABASE_CERT_LEN        "sup_cert_len"
#define NVS_KEY_EMAIL_CERT               "email_cert"
#define NVS_KEY_EMAIL_CERT_LEN           "email_cert_len"


#define NVS_KEY_SENSOR_PREFIX   "sens_"
#define NVS_KEY_RELAY_PREFIX    "relay_"

// ============================================================
// MODBUS Map Keys
// ============================================================
#define NVS_KEY_MODBUS_MAP      "mb_map"

// ============================================================
// Internal NVS Helpers
// ============================================================
esp_err_t wqms_nvs_set_str(const char *key, const char *value);
esp_err_t wqms_nvs_get_str(const char *key, char *buffer, size_t max_len);
esp_err_t wqms_nvs_set_u32(const char *key, uint32_t value);
uint32_t wqms_nvs_get_u32(const char *key, uint32_t default_val);

// ============================================================
// Function Prototypes
// ============================================================

void nvs_config_init(void);
void nvs_config_load(void);
void nvs_config_save(void);

// System config
const char* nvs_get_system_name(void);
void nvs_set_system_name(const char *name);

const char* nvs_get_system_location(void);
void nvs_set_system_location(const char *loc);

const char* nvs_get_timezone(void);
void nvs_set_timezone(const char *tz);

uint32_t nvs_get_sample_interval(void);
void nvs_set_sample_interval(uint32_t ms);

uint32_t nvs_get_automation_interval(void);
void nvs_set_automation_interval(uint32_t sec);

// Supabase configuration
const char* nvs_get_supabase_project_url(void);
const char* nvs_get_supabase_bucket_name(void);
void nvs_get_supabase_bucketlist_url(char*, size_t);
void nvs_get_supabase_bucketdownload_url(char*, size_t);
void nvs_get_supabase_sensor_url(char*, size_t);
void nvs_get_supabase_log_url(char*, size_t);
void nvs_get_supabase_sensorconfig_url(char*, size_t);
void nvs_get_supabase_systemconfig_url(char*, size_t);
const char* nvs_get_supabase_api_key(void);
uint32_t nvs_get_supabase_upload_interval(void);
void nvs_set_supabase_project_url(const char *url);
void nvs_set_supabase_bucket_name(const char *url);
void nvs_set_supabase_api_key(const char *key);
void nvs_set_supabase_upload_interval(uint32_t sec);
bool nvs_supabase_is_configured(void);

// ============================================================
// MODBUS Map Configuration
// ============================================================
typedef struct {
    uint16_t address;
    char type[16];
    char description[32];
    char access[8];
} modbus_map_entry_t;

void nvs_load_modbus_map(modbus_map_entry_t *map, int count);
void nvs_save_modbus_map(modbus_map_entry_t *map, int count);

// ============================================================
// Sensor Configuration
// ============================================================
void nvs_load_sensor_config(sensor_config_t *config, int count);
void nvs_save_sensor_config(sensor_config_t *config, int count);

// ============================================================
// Relay Configuration
// ============================================================
void nvs_load_relay_config(relay_config_t *config, int count);
void nvs_save_relay_config(relay_config_t *config, int count);

// ============================================================
// Date & Time Configuration
// ============================================================
esp_err_t nvs_save_datetime(void);
esp_err_t nvs_get_datetime(void);

// ============================================================
// Factory Reset
// ============================================================
void nvs_factory_reset(void);

#endif // NVS_CONFIG_H