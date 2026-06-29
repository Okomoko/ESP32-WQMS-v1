// project_defs.h
// Global type definitions used across all modules
// NOTE: log types are defined in log_levels.h - do not duplicate here

#ifndef PROJECT_DEFS_H
#define PROJECT_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include "system_config.h"

// ============================================================
// Sensor Types
// ============================================================
typedef enum {
    SENSOR_PH = 0,
    SENSOR_EC = 1,
    SENSOR_POTASSIUM = 2,
    SENSOR_MAGNESIUM = 3,
    SENSOR_IRON = 4,
    SENSOR_PHOSPHORUS = 5,
    SENSOR_CALCIUM = 6,
    SENSOR_NITROGEN = 7,
    SENSOR_TEMPERATURE = 8,
    SENSOR_HUMIDITY = 9
} sensor_id_t;

typedef enum {
    SENSOR_STATUS_OK = 0,
    SENSOR_STATUS_ERROR = 1,
    SENSOR_STATUS_CALIBRATING = 2,
    SENSOR_STATUS_DISABLED = 3,
    SENSOR_STATUS_OUT_OF_RANGE = 4
} sensor_status_t;

// Sensor configuration (stored in NVS)
typedef struct __attribute__((packed)) {
    char name[20];
    uint8_t enabled;
    uint8_t gpio_pin;
    uint8_t adc_channel;
    uint16_t modbus_register;
    uint16_t calibration_factor;
    float min_value;
    float max_value;
    uint8_t reserved[6];
} sensor_config_t;

// Sensor reading (latest value)
typedef struct {
    uint32_t timestamp;
    float value;
    uint16_t raw_adc;
    uint8_t status;
    uint8_t quality;
} sensor_reading_t;

// Sensor history record (stored in ring buffer)
typedef struct __attribute__((packed)) {
    uint32_t timestamp;
    uint16_t sensor_mask;
    uint16_t values[10];
} sensor_record_t;

// ============================================================
// Relay Types
// ============================================================
typedef enum { //Important !!!! HW-316 boards are active low, therefore RELAY_STATE_ACTIVE is 0 not 1 
    RELAY_STATE_ACTIVE = 0,
    RELAY_STATE_IDLE = 1,
    RELAY_STATE_COOLDOWN = 2
} relay_state_t;

typedef struct __attribute__((packed)) {
    char name[20];
    uint8_t enabled;
    uint8_t gpio_pin;
    uint16_t modbus_register;
    uint16_t activity_duration;
    uint16_t off_delay;
    uint8_t reserved[10];
} relay_config_t;

typedef struct {
    relay_config_t config;
    uint8_t state;
    uint32_t state_start_time;
    void *activity_timer;
    void *cooldown_timer;
} relay_instance_t;

// ============================================================
// Automation Types
// ============================================================
typedef enum {
    COMP_GREATER = 0,
    COMP_LESS = 1,
    COMP_EQUAL = 2,
    COMP_NOT_EQUAL = 3
} comparator_t;

typedef enum {
    LOGIC_AND = 0,
    LOGIC_OR = 1,
    LOGIC_NOT = 2
} logic_type_t;

typedef enum {
    OUTPUT_RELAY = 0,
    OUTPUT_EMAIL = 1
} output_type_t;

typedef struct {
    uint8_t sensor_id;
    comparator_t comparator;
    float threshold;
} condition_t;

typedef struct {
    output_type_t type;
    uint8_t id;
    uint16_t custom_duration;
} output_t;

typedef struct {
    int8_t rule_id;
    char name[32];
    uint8_t enabled;
    condition_t conditions[8];
    uint8_t condition_count;
    logic_type_t logic_type;
    output_t outputs[4];
    uint8_t output_count;
    uint8_t email_enabled;
    char email_recipient[64];
    char email_subject[64];
    uint8_t cooldown_enabled;
    uint16_t cooldown_seconds;
    uint32_t last_triggered;
    uint32_t trigger_count;
    char created_by[16];
    uint32_t created_timestamp;
    char last_modified_by[16];
    uint32_t last_modified_timestamp;
} automation_rule_t;

// ============================================================
// Watchdog Types
// ============================================================
typedef enum {
    WDT_MODULE_SENSOR = 0,
    WDT_MODULE_AUTOMATION,
    WDT_MODULE_MODBUS,
    WDT_MODULE_WEBSERVER,
    WDT_MODULE_INTEGRATION,
    WDT_MODULE_NTP,
    WDT_MODULE_LOGGING,
    WDT_MODULE_WIFI,
    WDT_MODULE_MAX
} wdt_module_t;

// ============================================================
// NTP Types
// ============================================================
typedef enum {
    NTP_MODE_FAST = 0,
    NTP_MODE_NORMAL = 1,
    NTP_MODE_BACKOFF = 2,
    NTP_MODE_MANUAL = 3
} ntp_mode_t;

#endif // PROJECT_DEFS_H